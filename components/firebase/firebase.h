#ifndef __FIREBASE_H__
#define __FIREBASE_H__

extern void firebase_stream_task(void *pvParameters);
extern char* firebase_get_data(const char *path);
extern char* firebase_post_data(const char *path, const char *json_data);

#endif /* __FIREBASE_H__ */