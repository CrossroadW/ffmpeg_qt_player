ffplay -f f32le -ar 48000 -ac 2 -i decode.pcm
ffplay -f rawvideo -pixel_format yuv420p -video_size 960x400 -framerate 23.98 decoded_video.yuv