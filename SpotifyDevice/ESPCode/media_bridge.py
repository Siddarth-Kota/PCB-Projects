import asyncio
import serial
import io
import struct
import time
import ctypes
from PIL import Image
from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionManager
from winsdk.windows.storage.streams import Buffer, InputStreamOptions

COM_PORT = 'COM7' 
BAUD_RATE = 921600

async def get_media_info():
    try:
        sessions = await GlobalSystemMediaTransportControlsSessionManager.request_async()
        session = sessions.get_current_session()
        if not session: return None, None, None, None, 0

        props = await session.try_get_media_properties_async()
        timeline = session.get_timeline_properties()
        
        pos = timeline.position.total_seconds()
        dur = timeline.end_time.total_seconds()
        progress_pct = int((pos / dur) * 100) if dur > 0 else 0
        
        album = props.album_title if props.album_title else "Single"
        
        img_data = None
        if props.thumbnail:
            stream = await props.thumbnail.open_read_async()
            size = stream.size
            buffer = Buffer(size)
            await stream.read_async(buffer, size, InputStreamOptions.NONE)
            address = ctypes.addressof(ctypes.c_char.from_buffer(buffer))
            raw_data = ctypes.string_at(address, size)
            img = Image.open(io.BytesIO(raw_data)).resize((180, 180)).convert("RGB")
            byte_arr = io.BytesIO()
            img.save(byte_arr, format='JPEG', quality=80, optimize=True)
            img_data = byte_arr.getvalue()

        return props.title, props.artist, album, img_data, progress_pct
    except:
        return None, None, None, None, 0

async def main():
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
        print(f"✅ Linked to {COM_PORT}")
    except Exception as e:
        print(f"❌ Error: {e}"); return

    last_title, last_progress = "", -1

    while True:
        title, artist, album, img_data, progress_pct = await get_media_info()
        
        if title and title != last_title:
            # Send TXT packet: TXT|Title|Artist|Album\n
            ser.write(f"TXT|{title}|{artist}|{album}\n".encode('utf-8'))
            await asyncio.sleep(0.4) 
            if img_data:
                ser.write(b"IMG|")
                ser.write(struct.pack('<I', len(img_data)))
                ser.write(img_data)
            last_title = title

        if title and progress_pct != last_progress:
            # Send PROG packet: PROG|Percent\n
            ser.write(f"PROG|{progress_pct}\n".encode('utf-8'))
            last_progress = progress_pct
            
        await asyncio.sleep(1)

if __name__ == "__main__":
    asyncio.run(main())