/*
 * micloud.h
 *
 *  Created on: 2020年10月24日
 *      Author: kang
 */

#ifndef SERVER_MICLOUD_MICLOUD_H_
#define SERVER_MICLOUD_MICLOUD_H_

#ifdef __cplusplus
extern "C" {
#endif

#define PROPERTIY_MASK 0xff
#define SERVICE_MASK   0xff00
#define MOTION_PICTURE_NAME                   "/tmp/motion.jpg"
//#define MOTION_PICTURE_NAME                   "/tmp/motion_spd.jpg"

#define   M_HANG_UP_FLAG             		1
#define   PUT_FRAME_PAUE_FLAG               2

int rpc_init();
int media_init();
int creat_video_thread(void);
int creat_audio_thread(void);
void main_thread_exit_termination(int arg);
#ifdef __cplusplus
}
#endif

#endif /* SERVER_MICLOUD_MICLOUD_H_ */
