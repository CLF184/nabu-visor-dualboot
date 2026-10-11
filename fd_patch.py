# fd_patch.py — 固件卷（FV/FFS）改写器：把 Nabu 菜单驱动插入 FD 的 DXE 固件卷。
#
# 来源：首次移植项目 nabu_gui_dualboot/patch.py（MIT）的 FD 改写部分，原样保留。
# 它已在真机上被证明可用（首次移植中菜单能显示 = 改写后的 FD 能被固件加载并派发驱动）。
# 本文件只负责 FD 改写；boot.img 打补丁仍走官方工具链（patch_boot.py）。
#!/usr/bin/env python3
"""Nabu GUI dualboot packer. No device writes; Python standard library only.

Understood containers: EFI FFS2/FV, LZMA guided sections and raw nested FVs,
Android boot header v3/v4 (unsigned), uncompressed or gzip ARM64 kernels.
Other Android boot versions can use a caller-supplied magiskboot executable.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
import lzma
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import uuid
import zlib
import zipfile

ROOT = Path(__file__).resolve().parent
FD_BASE = 0x9FC00000
FD_SIZE = 0x300000
DRIVER_GUID = uuid.UUID('dd685cee-7b4b-4d55-b7ab-e861d982eb3a').bytes_le
BOOT_SLOT_GUID = uuid.UUID('ba325ac5-10ab-44e3-b6c1-5bd273a3bf2d').bytes_le
BDS_FILE = uuid.UUID('6d33944a-ec75-4855-a54d-809c75241f6c').bytes_le
BDS_PROTOCOL = uuid.UUID('665e3ff6-46cc-11d4-9a38-0090273fc14d').bytes_le
BUTTONS_FILE = uuid.UUID('5bd181db-0487-4f1a-ae73-820e165611b3').bytes_le
LZMA_GUID = uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf').bytes_le
FFS2_GUID = uuid.UUID('8c8ce578-8a3d-4f1c-9935-896185c32dd3').bytes_le

class PatchError(ValueError):
    pass

def align(n, size):
    return (n + size - 1) // size * size

def sha(data):
    return hashlib.sha256(data).hexdigest()

def u24(data):
    return int.from_bytes(data[:3], 'little')

def section(kind, payload):
    size = len(payload) + 4
    if size >= 0xFFFFFF:
        return b'\xff\xff\xff' + bytes([kind]) + struct.pack('<I', len(payload)+8) + payload
    return size.to_bytes(3, 'little') + bytes([kind]) + payload

def iter_sections(data):
    off = 0
    while off + 4 <= len(data):
        if data[off:] in (b'\0' * (len(data)-off), b'\xff' * (len(data)-off)):
            return
        size, kind, hs = u24(data[off:]), data[off+3], 4
        if size == 0xFFFFFF:
            if off+8 > len(data): raise PatchError('Truncated extended section')
            size, hs = struct.unpack_from('<I',data,off+4)[0], 8
        if size < hs or off+size > len(data): raise PatchError('Malformed section bounds')
        yield off, kind, hs, data[off+hs:off+size]
        off = align(off+size, 4)

def check_pe(pe):
    if len(pe) < 0x100 or pe[:2] != b'MZ': raise PatchError('Driver is not a PE image')
    at=struct.unpack_from('<I',pe,0x3C)[0]
    if at+24+160 > len(pe) or pe[at:at+4] != b'PE\0\0': raise PatchError('Invalid PE header')
    machine, count = struct.unpack_from('<HH',pe,at+4)
    opt=at+24
    if machine != 0xAA64 or struct.unpack_from('<H',pe,opt)[0] != 0x20B:
        raise PatchError('Driver must be AArch64 PE32+')
    if struct.unpack_from('<H',pe,opt+68)[0] != 11:
        raise PatchError('Driver subsystem must be EFI boot service driver (11)')
    if struct.unpack_from('<II',pe,opt+112+5*8) == (0,0):
        raise PatchError('Driver lacks relocations; it cannot be loaded at arbitrary addresses')
    if struct.unpack_from('<II',pe,opt+112+1*8) != (0,0):
        raise PatchError('EFI driver must not import host DLLs')
    return {'machine':'AArch64','subsystem':11,'sections':count,'sha256':sha(pe)}

def ffs_checksum(file):
    out=bytearray(file)
    out[16]=0
    # State and data checksum do not participate in the header checksum.
    out[16]=(-sum(out[:24])+out[17]+out[23]) & 255
    out[17]=(-sum(out[24:])) & 255 if out[19] & 0x40 else 0xAA
    return bytes(out)

def make_driver(pe, erase=0xFF, guid=DRIVER_GUID, name='NabuBootMenu', depex=None):
    check_pe(pe)
    payload=section(0x13,depex if depex is not None else b'\x02'+BDS_PROTOCOL+b'\x08') # PUSH BDS; END
    payload+=b'\0'*(-len(payload)%4)
    payload+=section(0x10,pe)
    payload+=b'\0'*(-len(payload)%4)
    payload+=section(0x15,(name+'\0').encode('utf-16le'))
    size=24+len(payload)
    header=guid+b'\0\xaa'+bytes([7,0])+size.to_bytes(3,'little')+bytes([0xF8 if erase==0xFF else 7])
    return ffs_checksum(header+payload)

def parse_fv(data):
    if len(data)<72 or data[40:44]!=b'_FVH': raise PatchError('Missing FV header')
    if data[16:32]!=FFS2_GUID: raise PatchError('Only EFI FFS2 volumes are supported')
    size=struct.unpack_from('<Q',data,32)[0]
    hs=struct.unpack_from('<H',data,48)[0]
    if size>len(data) or hs<72 or hs>size or hs%2: raise PatchError('Invalid FV length/header')
    if sum(struct.unpack('<'+'H'*(hs//2),data[:hs])) & 65535:
        raise PatchError('Invalid FV header checksum')
    erase=255 if struct.unpack_from('<I',data,44)[0]&0x800 else 0
    off=align(hs,8)
    ext=struct.unpack_from('<H',data,52)[0]
    if ext:
        if ext+20>size: raise PatchError('Invalid FV extended header')
        off=align(max(off,ext+struct.unpack_from('<I',data,ext+16)[0]),8)
    files=[]
    while off+24<=size:
        if data[off:off+24] == bytes([erase])*24: break
        sz=u24(data[off+20:]);attr=data[off+19]
        if sz==0xFFFFFF or attr&1: raise PatchError('Extended/large FFS files are unsupported')
        if sz<24 or off+sz>size: raise PatchError('Invalid FFS length')
        file=data[off:off+sz]
        if (sum(file[:24])-file[17]-file[23])&255: raise PatchError('Invalid FFS header checksum')
        if attr&0x40 and (sum(file[24:])+file[17])&255: raise PatchError('Invalid FFS data checksum')
        state=file[23] ^ erase
        # Never repack deleted or incompletely written files into valid files.
        if state & 0x38 or not state&4: raise PatchError('Unsupported FFS lifecycle state')
        files.append((off,file))
        off=align(off+sz,8)
    if any(x!=erase for x in data[off:size]): raise PatchError('Non-erased bytes in FV free space')
    return size,hs,erase,files,off

def resize_fv_header(header, old_size, new_size):
    out=bytearray(header)
    blocks=[]
    off=56
    while off+8<=len(out):
        count,unit=struct.unpack_from('<II',out,off)
        if count==unit==0: break
        blocks.append((off,count,unit));off+=8
    if len(blocks)!=1 or blocks[0][1]*blocks[0][2]!=old_size:
        raise PatchError('Growing this FV block map is unsupported')
    at,_,unit=blocks[0]
    new_size=align(new_size,unit)
    struct.pack_into('<Q',out,32,new_size)
    struct.pack_into('<I',out,at,new_size//unit)
    struct.pack_into('<H',out,50,0)
    checksum=(-sum(struct.unpack('<'+'H'*(len(out)//2),out)))&65535
    struct.pack_into('<H',out,50,checksum)
    return bytes(out),new_size

def lzma_pack(raw):
    out=bytearray(lzma.compress(raw,format=lzma.FORMAT_ALONE,
        filters=[{'id':lzma.FILTER_LZMA1,'dict_size':1<<23,'lc':3,'lp':0,'pb':2,'mode':lzma.MODE_NORMAL,'nice_len':128}]))
    # EDK2 GetInfo reads this size before decompression. Python emits unknown
    # size (all FF); leaving that in an FD would request an enormous allocation.
    out[5:13]=struct.pack('<Q',len(raw))
    return bytes(out)

def lzma_unpack(data):
    if len(data)<13:raise PatchError('Truncated LZMA header')
    expected=struct.unpack_from('<Q',data,5)[0]
    limit=64*1024*1024
    if expected!=0xFFFFFFFFFFFFFFFF and expected>limit:
        raise PatchError('Decompressed section exceeds 64 MiB')
    def decode(encoded):
        decoder=lzma.LZMADecompressor(format=lzma.FORMAT_ALONE,memlimit=limit)
        raw=decoder.decompress(encoded,max_length=limit+1)
        if len(raw)>limit:raise PatchError('Decompressed section exceeds 64 MiB')
        if not decoder.eof:raise PatchError('Truncated LZMA stream')
        return raw
    try:raw=decode(data)
    except lzma.LZMAError as error:
        # liblzma <=5.2.5 rejects a valid known-size stream with an EOS marker.
        # Decode that case as unknown-size, then verify the original size. The
        # firmware header stays intact: EDK2 still needs the actual length.
        if expected==0xFFFFFFFFFFFFFFFF:raise PatchError('Invalid LZMA stream') from error
        compatible=data[:5]+b'\xff'*8+data[13:]
        try:raw=decode(compatible)
        except lzma.LZMAError as e:raise PatchError('Invalid LZMA stream') from e
    if expected!=0xFFFFFFFFFFFFFFFF and len(raw)!=expected:
        raise PatchError('LZMA uncompressed size mismatch')
    return raw

def rewrite_sections(data, pe, info, depth, slot_pe=None):
    if depth>8: raise PatchError('FV nesting limit exceeded')
    output=bytearray();changed=False
    for _,kind,hs,payload in iter_sections(data):
        new=payload
        if kind==0x17:
            new,c=patch_fv(payload,pe,info,depth+1,allow_grow=True,slot_pe=slot_pe);changed |= c
        elif kind==2:
            if len(payload)<20: raise PatchError('Truncated GUID-defined section')
            guid=payload[:16];data_offset,attrs=struct.unpack_from('<HH',payload,16)
            start=data_offset-hs
            if start<20 or start>len(payload): raise PatchError('Invalid guided data offset')
            if guid==LZMA_GUID:
                if attrs&2: raise PatchError('Authenticated LZMA sections cannot be repacked')
                raw=lzma_unpack(payload[start:])
                raw,c=rewrite_sections(raw,pe,info,depth+1,slot_pe)
                if c:new=payload[:start]+lzma_pack(raw);changed=True
        output.extend(section(kind,new));output.extend(b'\0'*(-len(output)%4))
    return (bytes(output) if changed else data),changed

def patch_fv(data, pe, info, depth=0, allow_grow=False, slot_pe=None):
    size,hs,erase,files,free=parse_fv(data)
    guids={f[:16] for _,f in files}
    target=BDS_FILE in guids and BUTTONS_FILE in guids
    if target: info['targets']+=1
    built=[];changed=False;driver_present=False
    for off,file in files:
        if file[:16]==DRIVER_GUID:
            if not target: raise PatchError('Menu driver found in unexpected volume')
            built.append((off,make_driver(pe,erase)));changed=True;driver_present=True
            continue
        if file[18] in (0xF0,1,2): built.append((off,file));continue
        body,c=rewrite_sections(file[24:],pe,info,depth+1,slot_pe)
        if c:
            file=bytearray(file[:24])+body
            if len(file)>=0xFFFFFF: raise PatchError('FFS would require extended size')
            file[20:23]=len(file).to_bytes(3,'little');file=ffs_checksum(file);changed=True
        built.append((off,file))
    if not target and not changed:return data,False
    # Preserve offsets of every untouched file. Growth of outer compressed FV
    # containers is allowed only into erased tail space, never over live files.
    out=bytearray(data[:size]);end=free
    for index,(off,file) in enumerate(built):
        next_off=built[index+1][0] if index+1<len(built) else size
        if off+len(file)>next_off: raise PatchError('Changed FFS overlaps next live file')
        old=u24(data[off+20:]);out[off:off+max(old,len(file))]=bytes([erase])*max(old,len(file))
        out[off:off+len(file)]=file
        if index+1==len(built):end=align(off+len(file),8)
    additions=[]
    if target and slot_pe is not None:
        # Preserve the original BootSlotDxe GUID and TRUE dependency expression.
        additions.append(('boot_slot',make_driver(slot_pe,erase,BOOT_SLOT_GUID,'BootSlotDxe',b'\x06\x08')))
    if target and not driver_present:
        additions.append(('menu',make_driver(pe,erase)))
    for label,driver in additions:
        needed=end+align(len(driver),8)
        if needed>size:
            if not allow_grow: raise PatchError('Top-level FV has insufficient space')
            header,new_size=resize_fv_header(out[:hs],size,needed)
            out.extend(bytes([erase])*(new_size-size));out[:hs]=header;size=new_size
        out[end:end+len(driver)]=driver;changed=True
        info['driver_offset_in_dxe_fv' if label=='menu' else 'boot_slot_offset_in_dxe_fv']=end
        info['dxe_fv_size']=size
        end=needed
    return bytes(out)+data[struct.unpack_from('<Q',data,32)[0]:],changed

def patch_fd(fd, pe, slot_pe=None):
    if len(fd)!=FD_SIZE: raise PatchError('Nabu FD must be exactly 3 MiB for this memory map')
    if slot_pe is not None and inspect_fd(fd)['boot_slot_drivers']:
        raise PatchError('Input already contains BootSlotDxe; preserve it instead of inserting a duplicate')
    info={'targets':0,'driver':check_pe(pe)}
    if slot_pe is not None:info['boot_slot_driver']=check_pe(slot_pe)
    result,changed=patch_fv(fd,pe,info,slot_pe=slot_pe)
    if not changed or info['targets']!=1: raise PatchError('Expected one DXE FV containing Nabu ButtonsDxe and BdsDxe')
    if len(result)!=len(fd): raise PatchError('FD size changed unexpectedly')
    # A complete second parse verifies every rebuilt checksum/container.
    inventory=inspect_fd(result)
    if inventory['menu_drivers']!=1: raise PatchError('Menu driver insertion verification failed')
    if slot_pe is not None and inventory['boot_slot_drivers']!=1:
        raise PatchError('BootSlotDxe insertion verification failed')
    info.update(inventory);info['sha256']=sha(result)
    return result,info

def inspect_fd(fd):
    result={'menu_drivers':0,'boot_slot_drivers':0,'bds_drivers':0,'buttons_drivers':0,'volumes':0,'files':0}
    def sections(data,depth):
        if depth>8: raise PatchError('FV nesting limit exceeded')
        for _,kind,hs,p in iter_sections(data):
            if kind==0x17:volume(p,depth+1)
            elif kind==2 and p[:16]==LZMA_GUID:
                at=struct.unpack_from('<H',p,16)[0]-hs
                sections(lzma_unpack(p[at:]),depth+1)
    def volume(data,depth=0):
        _,_,_,files,_=parse_fv(data);result['volumes']+=1
        for _,f in files:
            result['files']+=1
            result['menu_drivers']+=f[:16]==DRIVER_GUID
            result['boot_slot_drivers']+=f[:16]==BOOT_SLOT_GUID
            result['bds_drivers']+=f[:16]==BDS_FILE
            result['buttons_drivers']+=f[:16]==BUTTONS_FILE
            if f[18] not in (0xF0,1,2): sections(f[24:],depth+1)
    volume(fd)
    return result

def branch_target(word,at):
    if word&0xFC000000!=0x14000000: raise PatchError('Expected an ARM64 B instruction')
    immediate=word&0x3FFFFFF
    if immediate&0x2000000:immediate-=0x4000000
    return at+immediate*4

def patch_kernel(kernel, fd, shim):
    tail=b'';compressed=False
    if kernel[:2]==b'\x1f\x8b':
        decoder=zlib.decompressobj(31)
        try:raw=decoder.decompress(kernel);tail=decoder.unused_data
        except zlib.error as e: raise PatchError('Invalid gzip kernel') from e
        if not decoder.eof:raise PatchError('Truncated gzip kernel')
        kernel=raw;compressed=True
    if len(kernel)<len(shim) or kernel[56:60]!=b'ARMd' or len(shim)<128 or len(shim)>512:
        raise PatchError('Unsupported ARM64 kernel or invalid shim')
    if branch_target(struct.unpack_from('<I',shim)[0],0)!=64:raise PatchError('Shim entry must branch to 0x40')
    # Extract only a linked shim: unresolved ADR relocations from a .o would
    # silently copy from the selector instead of the appended firmware.
    if len(shim)%4:raise PatchError('Shim instructions are not aligned')
    addresses=[]
    for at in range(64,len(shim),4):
        word=struct.unpack_from('<I',shim,at)[0]
        if word&0x9F00001F==0x10000004: # ADR x4, kernel header
            immediate=((word>>29)&3)|(((word>>5)&0x7FFFF)<<2)
            if immediate&0x100000:immediate-=0x200000
            addresses.append(at+immediate)
    if addresses!=[0]:raise PatchError('Shim must contain a linked ADR x4 to the kernel header')
    if len(fd)!=FD_SIZE:raise PatchError('Appended FD must be exactly 3 MiB')
    first,second=struct.unpack_from('<II',kernel)
    # ARM64 EFI-stub headers contain PE metadata after the Image header.
    # Refuse instead of blindly destroying it like older patchers.
    if kernel[:2]==b'MZ':raise PatchError('EFI-stub kernels are unsupported; supply a non-EFI Android kernel')
    destination=branch_target(first,0)
    if destination==64:
        destination=branch_target(second,4)
        base,size,original=struct.unpack_from('<QQQ',kernel,32)
        if base!=FD_BASE or size!=FD_SIZE or not len(shim)<=original<=len(kernel):
            raise PatchError('Existing patch has invalid kernel/FD bounds')
        if len(kernel)!=original and (len(kernel)!=original+FD_SIZE or kernel[original+40:original+44]!=b'_FVH'):
            raise PatchError('Existing DBKP payload does not match recorded length')
        # Accept only known Aloha or this project's selector prefixes.
        if kernel[64:68] not in (bytes.fromhex('840092d2'),bytes.fromhex('040080d2')):
            raise PatchError('Unknown existing selector; refusing to overwrite it')
        kernel=kernel[:original]
    else:
        # Unpatched or inactive DBKP image: insertion headroom must be empty or
        # recognizable selector code, rather than live kernel instructions.
        area=kernel[64:len(shim)]
        if any(area) and kernel[64:68] not in (bytes.fromhex('840092d2'),bytes.fromhex('040080d2')):
            raise PatchError('No safe headroom for selector code')
    if not len(shim)<=destination<len(kernel):raise PatchError('Original Android branch is outside kernel')
    original=align(len(kernel),16)
    k=bytearray(kernel+b'\0'*(original-len(kernel)))
    struct.pack_into('<II',k,0,0x14000010,0x14000000|((destination-4)//4&0x3FFFFFF))
    struct.pack_into('<QQQ',k,32,FD_BASE,FD_SIZE,original)
    k[64:len(shim)]=shim[64:]
    k.extend(fd)
    output=bytes(k)
    if compressed:output=gzip.compress(output,mtime=0)+tail
    return output,{'android_entry':hex(destination),'original_kernel_bytes':original,
        'fd_offset':hex(original),'fd_base':hex(FD_BASE),'fd_size':FD_SIZE,'gzip':compressed,
        'kernel_sha256':sha(output)}

def repack_boot(boot,fd,shim):
    if len(boot)<4096 or boot[:8]!=b'ANDROID!':raise PatchError('Input is not an Android boot.img')
    version=struct.unpack_from('<I',boot,40)[0]
    if version not in (3,4):raise PatchError('Native packer supports header v3/v4; use --magiskboot for v0/v1/v2')
    ks,rs=struct.unpack_from('<II',boot,8)
    hs=struct.unpack_from('<I',boot,20)[0]
    if hs!=(1580 if version==3 else 1584) or not ks or not rs:
        raise PatchError('Invalid boot header or missing kernel/ramdisk')
    if version==4 and struct.unpack_from('<I',boot,1580)[0]:raise PatchError('Signed boot v4 is unsupported')
    ramdisk_at=4096+align(ks,4096);end=ramdisk_at+align(rs,4096)
    if end>len(boot):raise PatchError('Truncated boot payload')
    footer=boot[-64:-60]==b'AVBf'
    trailer=boot[end:]
    if not footer and any(trailer):raise PatchError('Unknown boot trailer; refusing to discard it')
    kernel=boot[4096:4096+ks];ramdisk=boot[ramdisk_at:ramdisk_at+rs]
    patched,info=patch_kernel(kernel,fd,shim)
    header=bytearray(boot[:4096]);struct.pack_into('<I',header,8,len(patched))
    result=bytes(header)+patched+b'\0'*(-len(patched)%4096)+ramdisk+b'\0'*(-len(ramdisk)%4096)
    if len(result)>len(boot):raise PatchError('Output exceeds input image capacity')
    result+=b'\0'*(len(boot)-len(result))
    info.update({'boot_header':version,'ramdisk_unchanged':True,'ramdisk_sha256':sha(ramdisk),
                 'avb_footer_removed':footer,'boot_bytes':len(result),'boot_sha256':sha(result)})
    # Check the serialized result, not only the in-memory component buffers.
    nk,nr=struct.unpack_from('<II',result,8);new_rd_at=4096+align(nk,4096)
    if result[new_rd_at:new_rd_at+nr]!=ramdisk:raise PatchError('Repacked ramdisk verification failed')
    return result,info

def repack_with_magiskboot(boot_path,fd,shim,tool):
    tool=Path(tool).resolve()
    if not tool.is_file():raise PatchError('magiskboot executable not found')
    with tempfile.TemporaryDirectory(prefix='nabu-gui-') as work:
        wd=Path(work)
        def run(*args):
            p=subprocess.run([str(tool),*args],cwd=wd,capture_output=True,text=True)
            if p.returncode:raise PatchError('magiskboot failed: '+p.stdout+p.stderr)
        run('unpack',str(boot_path.resolve()))
        raw=(wd/'kernel').read_bytes();patched,info=patch_kernel(raw,fd,shim)
        ramdisk=(wd/'ramdisk.cpio').read_bytes() if (wd/'ramdisk.cpio').exists() else None
        (wd/'kernel').write_bytes(patched);run('repack',str(boot_path.resolve()))
        result=(wd/'new-boot.img').read_bytes()
        # Independently unpack the completed boot to verify both components.
        for p in list(wd.iterdir()):
            if p.name!='new-boot.img' and p.is_file():p.unlink()
        run('unpack',str(wd/'new-boot.img'))
        if (wd/'kernel').read_bytes()!=patched:raise PatchError('magiskboot changed patched kernel unexpectedly')
        if ramdisk is not None and (wd/'ramdisk.cpio').read_bytes()!=ramdisk:
            raise PatchError('magiskboot changed ramdisk contents')
        if len(result)>boot_path.stat().st_size:raise PatchError('Output exceeds input image capacity')
        info.update({'magiskboot':str(tool),'ramdisk_unchanged':True,'boot_bytes':len(result),'boot_sha256':sha(result)})
        return result,info

def write_new(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    # Refuse accidental overwrite, including the input images.
    with path.open('xb') as f:f.write(data)

def write_esp_archive(out,manifest):
    out=Path(out)
    if not manifest.get('esp'):return
    archive=out/'nabu-visor-esp.zip'
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as packed:
        for name in manifest['esp']['files']:
            packed.write(out/'esp'/name,name)
    manifest['esp']['archive']={'name':archive.name,'sha256':sha(archive.read_bytes())}

def build(fd_path,boot_path,out_dir,driver_path=None,shim_path=None,magiskboot=None):
    fd_path=Path(fd_path);boot_path=Path(boot_path);out=Path(out_dir)
    driver_path=Path(driver_path or ROOT/'build/NabuBootMenu.efi')
    shim_path=Path(shim_path or ROOT/'build/ShellCode.NabuGui.bin')
    names=['nabu-gui.fd','nabu-gui-dualboot.img','manifest.json']
    if any((out/n).exists() for n in names+['esp','nabu-visor-esp.zip']):raise PatchError('Output exists; choose a new output directory')
    fd=fd_path.read_bytes();boot=boot_path.read_bytes()
    patched_fd,fd_info=patch_fd(fd,driver_path.read_bytes())
    if magiskboot:result,boot_info=repack_with_magiskboot(boot_path,patched_fd,shim_path.read_bytes(),magiskboot)
    else:result,boot_info=repack_boot(boot,patched_fd,shim_path.read_bytes())
    manifest={'format':1,'device':'xiaomi-nabu','hardware_tested':False,
        'input_fd':{'path':str(fd_path.resolve()),'sha256':sha(fd)},
        'input_boot':{'path':str(boot_path.resolve()),'sha256':sha(boot)},
        'fd':fd_info,'boot':boot_info,'shim_sha256':sha(shim_path.read_bytes()),
        'behavior':{'configuration':'ESP \\EFI\\visor\\boot.conf; editable upstream format','volume':'select','power':'confirm','auto_boot_scope':'upstream countdown; any input cancels','android':'chainload NabuAndroid.efi: consumed DRAM marker + warm reset','windows':'upstream EFI LoadImage/StartImage of configured bootmgfw.efi'},
        'notes':['Requires unlocked Android bootloader.','Preserves FD size and original firmware drivers.',
                 'Firmware boot policy must not already auto-reboot to Android before BDS.','Validate on Nabu hardware before permanent flashing.']}
    provenance = ROOT/'build/visor-build.json'
    if provenance.is_file():
        menu_info = json.loads(provenance.read_text(encoding='utf-8'))
        if menu_info.get('driver_sha256') == sha(driver_path.read_bytes()):
            manifest['menu'] = menu_info
    esp_source=ROOT/'esp'
    resources=[]
    if manifest.get('menu',{}).get('profile')=='full':
        for required in ['EFI/visor/boot.conf','EFI/visor/NabuAndroid.efi']:
            if not (esp_source/required).is_file():raise PatchError('Missing ESP file: '+required+'; run build.py first')
        resources=sorted(p for p in esp_source.rglob('*') if p.is_file())
        manifest['esp']={'required':True,'install':'Copy esp/EFI/visor into the existing ESP /EFI/visor directory',
            'files':{p.relative_to(esp_source).as_posix():sha(p.read_bytes()) for p in resources}}
    # Input hashes are rechecked before writing final artifacts.
    if sha(fd_path.read_bytes())!=sha(fd) or sha(boot_path.read_bytes())!=sha(boot):
        raise PatchError('Inputs changed during build')
    out.mkdir(parents=True,exist_ok=True)
    write_new(out/names[0],patched_fd);write_new(out/names[1],result)
    for resource in resources:write_new(out/'esp'/resource.relative_to(esp_source),resource.read_bytes())
    write_esp_archive(out,manifest)
    write_new(out/names[2],json.dumps(manifest,ensure_ascii=False,indent=2).encode('utf-8'))
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fd',required=True,type=Path)
    parser.add_argument('--boot',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--driver',type=Path)
    parser.add_argument('--shim',type=Path)
    parser.add_argument('--magiskboot',type=Path)
    args=parser.parse_args()
    try:
        info=build(args.fd,args.boot,args.out,args.driver,args.shim,args.magiskboot)
        print(json.dumps(info,ensure_ascii=False,indent=2))
    except (PatchError,OSError,lzma.LZMAError) as e:
        parser.exit(1,'ERROR: '+str(e)+'\n')
if __name__=='__main__':main()
