from pathlib import Path
import sys
p=Path(sys.argv[1])
b=bytearray(p.read_bytes())
old=b'api-ms-win-crt-private-l1-1-0.dll\0'
new=b'ucrtbase.dll\0'+b'\0'*(len(old)-len(b'ucrtbase.dll\0'))
count=b.count(old)
if count != 1:
    raise SystemExit(f'expected one private CRT import, found {count}')
p.write_bytes(b.replace(old,new,1))
print('patched private CRT import to ucrtbase.dll:',p)
