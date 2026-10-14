import asyncio
R=b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 6\r\nConnection: close\r\n\r\nhello\n"
async def h(r,w):
    try:
        await r.readuntil(b"\r\n\r\n"); w.write(R); await w.drain()
    except Exception: pass
    w.close()
async def main():
    s=await asyncio.start_server(h,"127.0.0.1",9002,backlog=1024)
    async with s: await s.serve_forever()
asyncio.run(main())