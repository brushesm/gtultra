#ifndef GVIDEO_H
#define GVIDEO_H

#include "bme/SDL/SDL.h"

typedef struct GTOBJECT GTOBJECT;

#ifdef GTULTRA_VIDEO
int gt_video_enabled(void);
int gt_video_load(const char *path);
void gt_video_close(void);
int gt_video_is_loaded(void);
int gt_video_is_video_path(const char *path);
int gt_video_handle_sdl_event(const SDL_Event *event);
void gt_video_tick(GTOBJECT *gt);
#else
static inline int gt_video_enabled(void) { return 0; }
static inline int gt_video_load(const char *path) { (void)path; return 0; }
static inline void gt_video_close(void) {}
static inline int gt_video_is_loaded(void) { return 0; }
static inline int gt_video_is_video_path(const char *path) { (void)path; return 0; }
static inline int gt_video_handle_sdl_event(const SDL_Event *event) { (void)event; return 0; }
static inline void gt_video_tick(GTOBJECT *gt) { (void)gt; }
#endif

#endif
