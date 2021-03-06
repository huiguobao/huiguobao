#include "network.h"
#include "detection_layer.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "image.h"
#include "demo.h"
#include <sys/time.h>
#include <stdbool.h>
#define DEMO 1

#ifdef OPENCV

static char **demo_names;
static char **ocr_names;

static image **demo_alphabet;
static int demo_classes;

static network *net;
static network *ocr_net;


static image buff [4];
static image buff_letter[4];

static int buff_index = 0;
static void * cap;
static float fps = 0;
static float demo_thresh = 0;
static float demo_hier = .5;

static int box_index = 0;

//static int running = 0;

static box *detect_box[2];
static int box_count[2];


//static int demo_frame = 3;
//static int demo_index = 0;
//static float **predictions;
//static float *avg;
static int demo_done = 0;
//static int demo_total = 0;
double demo_time;

detection *get_network_boxes(network *net, int w, int h, float thresh, float hier, int *map, int relative, int *num);

int size_network(network *net)
{
    int i;
    int count = 0;
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            count += l.outputs;
        }
    }
    return count;
}
/*
void remember_network(network *net)
{
    int i;
    int count = 0;
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            memcpy(predictions[demo_index] + count, net->layers[i].output, sizeof(float) * l.outputs);
            count += l.outputs;
        }
    }
}

detection *avg_predictions(network *net, int *nboxes)
{
    int i, j;
    int count = 0;
    fill_cpu(demo_total, 0, avg, 1);//fill avg with 0,demo_total is size of all values(float) of l.output, 
    for(j = 0; j < demo_frame; ++j){
        axpy_cpu(demo_total, 1./demo_frame, predictions[j], 1, avg, 1);//from predictions[j] get value multiply by 1./3 copy to avg, 3 frame average value into avg, 1/3+1/3+1/3
    }
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            memcpy(l.output, avg + count, sizeof(float) * l.outputs);//clear l.output with 0
            count += l.outputs;
        }
    }
    detection *dets = get_network_boxes(net, buff[0].w, buff[0].h, demo_thresh, demo_hier, 0, 1, nboxes);
    return dets;
}
*/
void *detect_in_thread(void *ptr)
{
    //printf("lp detect!\n");
    //running = 1;
    float nms = .4;

    layer l = net->layers[net->n-1];
    float *X = buff_letter[(buff_index+3)%4].data;
    network_predict(net, X);//forward net

    /*
       if(l.type == DETECTION){
       get_detection_boxes(l, 1, 1, demo_thresh, probs, boxes, 0);
       } else */
    //remember_network(net);//net->layers[i].output memcpy to predictions[demo_index] + count
    detection *dets = 0;
    int nboxes = 0;
    //dets = avg_predictions(net, &nboxes);
    dets = get_network_boxes(net, buff[0].w, buff[0].h, demo_thresh, demo_hier, 0, 1, &nboxes);

    /*
       int i,j;
       box zero = {0};
       int classes = l.classes;
       for(i = 0; i < demo_detections; ++i){
       avg[i].objectness = 0;
       avg[i].bbox = zero;
       memset(avg[i].prob, 0, classes*sizeof(float));
       for(j = 0; j < demo_frame; ++j){
       axpy_cpu(classes, 1./demo_frame, dets[j][i].prob, 1, avg[i].prob, 1);
       avg[i].objectness += dets[j][i].objectness * 1./demo_frame;
       avg[i].bbox.x += dets[j][i].bbox.x * 1./demo_frame;
       avg[i].bbox.y += dets[j][i].bbox.y * 1./demo_frame;
       avg[i].bbox.w += dets[j][i].bbox.w * 1./demo_frame;
       avg[i].bbox.h += dets[j][i].bbox.h * 1./demo_frame;
       }
    //copy_cpu(classes, dets[0][i].prob, 1, avg[i].prob, 1);
    //avg[i].objectness = dets[0][i].objectness;
    }
     */

    if (nms > 0) do_nms_obj(dets, nboxes, l.classes, nms);

    //printf("\033[2J");
    //printf("\033[1;1H");
    //printf("\nFPS:%.1f\n",fps);
    //printf("Objects:\n\n");

    float thresh = 0.5;
    int count = 0;
    //image display = buff[(buff_index+4) % 4];
    box obj_box[nboxes];
    box_count[box_index%2] = 0;
    int i;  
    for (i = 0; i < nboxes; ++i){
	    int j;
	    int sign = -1;

	    for(j = 0; j < l.classes; ++j){
		if (dets[i].prob[j] > thresh) sign = j;
	    } 

	    if (sign >= 0){
              obj_box[count] = dets[i].bbox;
              ++count;
	    }
    }
    if (count > 0){
       printf("lp detect count: %d\n", count);
       detect_box[box_index%2] = calloc(count, sizeof(box));
       memcpy(detect_box[box_index%2], obj_box, (count)*sizeof(box));
       box_count[box_index%2] = count;
    }

    //draw_detections(display, dets, nboxes, demo_thresh, demo_names, demo_alphabet, l.classes);
    free_detections(dets, nboxes);

    //demo_index = (demo_index + 1)%demo_frame;
    //running = 0;
    return 0;
}


void *ocr_detect_in_thread(void *ptr)
{
    //running = 1;
    //printf("ocr detect!\n");
    float thresh = 0.5;
    float hier_thresh = 0.5;
    float nms = .4;
    char ch_name[20] = {0};
    layer l = ocr_net->layers[ocr_net->n-1];

    image big_im = buff[(buff_index+2) % 4];

    int b_index = (box_index+1)%2;
    //printf("have object   ocr detect:%d!\n",b_index);
    int count = box_count[b_index];
    if (count){
       printf("ocr detect:%d\n", count);
       box boxes[count];
       memcpy(boxes, detect_box[b_index], count*sizeof(box));
       free(detect_box[b_index]);
       int i;
       //printf("count: %d\n", count);
       for (i = 0; i < count; ++i){

           image crop_im = get_crop_detect(big_im, boxes[i]);
           if (crop_im.w == 0) break;

           network_predict_image(ocr_net, crop_im);

           int nboxes = 0;

           detection *dets = get_network_boxes(ocr_net, crop_im.w, crop_im.h, thresh, hier_thresh, 0, 1, &nboxes);
           //printf("ocr detect nboxes: %d\n", nboxes);
           if (nms > 0) do_nms_sort(dets, nboxes, l.classes, nms);
           do_dets_sort(dets, nboxes);

           get_chinese_lp(dets, nboxes, thresh, ocr_names, l.classes, ch_name);

           if (strlen(ch_name)){
              //sprintf(ch_name, "%s%04d", outfile, i);//"%s%04d":reserve four 0
              //save_image(im_crop, ch_name);
              printf("chinese LP name:%s\n", ch_name);
           }
           free_detections(dets, nboxes);
           free_image(crop_im);
           draw_bbox(big_im, boxes[i], 5, 1,0,0);
       }

    }
    return 0;
}





void *fetch_in_thread(void *ptr)
{
    //printf("fetch image!\n");
    free_image(buff[buff_index]);
    buff[buff_index] = get_image_from_stream(cap);
    if(buff[buff_index].data == 0) {
        demo_done = 1;
        return 0;
    }
    letterbox_image_into(buff[buff_index], net->w, net->h, buff_letter[buff_index]);
    return 0;
}



void *display_in_thread(void *ptr)
{
    //printf("display image!\n");
    int c = show_image(buff[(buff_index + 1) % 4], "Demo", 1);
    if (c != -1) c = c%256;
    if (c == 27) {
        demo_done = 1;
        return 0;
    } else if (c == 82) {
        demo_thresh += .02;
    } else if (c == 84) {
        demo_thresh -= .02;
        if(demo_thresh <= .02) demo_thresh = .02;
    } else if (c == 83) {
        demo_hier += .02;
    } else if (c == 81) {
        demo_hier -= .02;
        if(demo_hier <= .0) demo_hier = .0;
    }
    return 0;
}
/*
void *display_loop(void *ptr)
{
    while(1){
        display_in_thread(0);
    }
}

void *detect_loop(void *ptr)
{
    while(1){
        detect_in_thread(0);
    }
}
*/
void demo(int cam_index, const char *filename, char *prefix, int avg_frames, int w, int h, int frames, int fullscreen)
{
    //demo_frame = avg_frames;
    image **alphabet = load_alphabet();
    demo_alphabet = alphabet;
    demo_thresh = 0.5;
    demo_hier = 0.5;
    printf("Demo\n");


    metadata meta = get_metadata("./lp_net/lpdetect/lp.data");
    demo_names = meta.names;
    demo_classes = meta.classes;
    net = load_network("./lp_net/lpdetect/yolov3-lp.cfg", "./lp_net/lpdetect/yolov3-lp_final.weights", 0);

    metadata ocr_meta = get_metadata("./lp_net/lpscr/lpscr.data");
    ocr_names = ocr_meta.names;
    ocr_net = load_network("./lp_net/lpscr/lpscr-net.cfg", "./lp_net/lpscr/lpscr-net_final.weights", 0);




    set_batch_network(net, 1);
    pthread_t detect_thread;
    pthread_t ocr_detect_thread;
    pthread_t fetch_thread;

    srand(2222222);

//    int i;
//    demo_total = size_network(net);//net output number:l.type == YOLO, REGION, DETECTION, if l.type == YOLO, then demo_total = 1
//    predictions = calloc(demo_frame, sizeof(float*));//demo_frame = 3, pointer to pointer, one pointer to three tuple pointer
//    for (i = 0; i < demo_frame; ++i){
//        predictions[i] = calloc(demo_total, sizeof(float));//calloc net output layer size, one YOLO + one YOLO + one YOLO
//    }
//    avg = calloc(demo_total, sizeof(float));//calloc net output layer size, pointer to (1 frame + 1 frame + 1 frame)/3

    if(filename){
        printf("video file: %s\n", filename);
        cap = open_video_stream(filename, 0, 0, 0, 0);
    }else{
        cap = open_video_stream(0, cam_index, w, h, frames);
    }

    if(!cap) error("Couldn't connect to webcam.\n");

    buff[0] = get_image_from_stream(cap);
    buff[1] = copy_image(buff[0]);
    buff[2] = copy_image(buff[0]);
    buff[3] = copy_image(buff[0]);

    buff_letter[0] = letterbox_image(buff[0], net->w, net->h);//image embed into boxed(net->w,net->h)
    buff_letter[1] = letterbox_image(buff[0], net->w, net->h);
    buff_letter[2] = letterbox_image(buff[0], net->w, net->h);
    buff_letter[3] = letterbox_image(buff[0], net->w, net->h);


    int count = 0;
    if(!prefix){
        make_window("Demo", 1352, 1013, fullscreen);
    }

    demo_time = what_time_is_it_now();

    while(!demo_done){
        buff_index = (buff_index + 1) % 4;
        box_index = (box_index + 1) % 2;
        double time;
        time=what_time_is_it_now();
        if(pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
        if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
        if(pthread_create(&ocr_detect_thread, 0, ocr_detect_in_thread, 0)) error("Thread creation failed");
        if(!prefix){
            fps = 1./(what_time_is_it_now() - demo_time);
            demo_time = what_time_is_it_now();
            display_in_thread(0);
        }else{
            char name[256];
            sprintf(name, "%s_%08d", prefix, count);
            save_image(buff[(buff_index + 1)%4], name);
        }
        pthread_join(fetch_thread, 0);
        pthread_join(detect_thread, 0);
        pthread_join(ocr_detect_thread, 0);
        ++count;
        printf("multi threads take in %f seconds.\n", what_time_is_it_now()-time);
    }
}

/*
   void demo_compare(char *cfg1, char *weight1, char *cfg2, char *weight2, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg_frames, float hier, int w, int h, int frames, int fullscreen)
   {
   demo_frame = avg_frames;
   predictions = calloc(demo_frame, sizeof(float*));
   image **alphabet = load_alphabet();
   demo_names = names;
   demo_alphabet = alphabet;
   demo_classes = classes;
   demo_thresh = thresh;
   demo_hier = hier;
   printf("Demo\n");
   net = load_network(cfg1, weight1, 0);
   set_batch_network(net, 1);
   pthread_t detect_thread;
   pthread_t fetch_thread;

   srand(2222222);

   if(filename){
   printf("video file: %s\n", filename);
   cap = cvCaptureFromFile(filename);
   }else{
   cap = cvCaptureFromCAM(cam_index);

   if(w){
   cvSetCaptureProperty(cap, CV_CAP_PROP_FRAME_WIDTH, w);
   }
   if(h){
   cvSetCaptureProperty(cap, CV_CAP_PROP_FRAME_HEIGHT, h);
   }
   if(frames){
   cvSetCaptureProperty(cap, CV_CAP_PROP_FPS, frames);
   }
   }

   if(!cap) error("Couldn't connect to webcam.\n");

   layer l = net->layers[net->n-1];
   demo_detections = l.n*l.w*l.h;
   int j;

   avg = (float *) calloc(l.outputs, sizeof(float));
   for(j = 0; j < demo_frame; ++j) predictions[j] = (float *) calloc(l.outputs, sizeof(float));

   boxes = (box *)calloc(l.w*l.h*l.n, sizeof(box));
   probs = (float **)calloc(l.w*l.h*l.n, sizeof(float *));
   for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = (float *)calloc(l.classes+1, sizeof(float));

   buff[0] = get_image_from_stream(cap);
   buff[1] = copy_image(buff[0]);
   buff[2] = copy_image(buff[0]);
   buff_letter[0] = letterbox_image(buff[0], net->w, net->h);
   buff_letter[1] = letterbox_image(buff[0], net->w, net->h);
   buff_letter[2] = letterbox_image(buff[0], net->w, net->h);
   ipl = cvCreateImage(cvSize(buff[0].w,buff[0].h), IPL_DEPTH_8U, buff[0].c);

   int count = 0;
   if(!prefix){
   cvNamedWindow("Demo", CV_WINDOW_NORMAL); 
   if(fullscreen){
   cvSetWindowProperty("Demo", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
   } else {
   cvMoveWindow("Demo", 0, 0);
   cvResizeWindow("Demo", 1352, 1013);
   }
   }

   demo_time = what_time_is_it_now();

   while(!demo_done){
buff_index = (buff_index + 1) %3;
if(pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
if(!prefix){
    fps = 1./(what_time_is_it_now() - demo_time);
    demo_time = what_time_is_it_now();
    display_in_thread(0);
}else{
    char name[256];
    sprintf(name, "%s_%08d", prefix, count);
    save_image(buff[(buff_index + 1)%3], name);
}
pthread_join(fetch_thread, 0);
pthread_join(detect_thread, 0);
++count;
}
}
*/
#else
void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg, float hier, int w, int h, int frames, int fullscreen)
{
    fprintf(stderr, "Demo needs OpenCV for webcam images.\n");
}
#endif

