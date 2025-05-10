ffmpeg -stream_loop -1 -re -i /home/awe/Videos/oceans.mp4 \
-c copy -f flv rtmp://localhost:1935/live/stream