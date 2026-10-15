#ifndef VIDEO_H
#define VIDEO_H
void video_init(void);
void video_set_palette(int idx,int r,int g,int b);
int video_in_vsync(void);
void video_wait_vsync(void);
#endif
