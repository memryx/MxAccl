import os
import sys
import cv2 as cv
import numpy as np
from collections import deque
import argparse
from multiprocessing import Process, Queue, Event
from apps import Cartoonizer, PoseEstimation
from PyQt5.QtWidgets import QApplication
from displayer import Displayer

def parse_args():
    parser = argparse.ArgumentParser(description="Cartoonizer and Pose Estimation demo with Multi-DFP")
    parser.add_argument('--cam', action='store_true', help="Use webcam instead of video")
    parser.add_argument('--show', action='store_true', help="Show output video")
    parser.add_argument("--old_bind", action="store_true", help="Use old binding method")
    parser.add_argument('--video', type=str, help="Path to video file")
    parser.add_argument('--frame_limit', '-f', type=int, default=30, help="Number of frames to process before swapping out.")
    parser.add_argument('--dfp_cartoon', type=str, default='/home/jackyyeh/memryx/mx_examples/multi_dfp_application/cartoonizer_pose/models/Facial_cartoonizer_512_512_3_onnx.dfp',
                        help="Path to the compiled DFP file for Cartoonizer")
    parser.add_argument('--dfp_pose', type=str, default='/home/jackyyeh/memryx/mx_examples/multi_dfp_application/cartoonizer_pose/models/YOLO_v8_small_pose_640_640_3_onnx.dfp',
                        help="Path to the compiled DFP file for Pose Estimation")
    parser.add_argument('--post_model', type=str, default='/home/jackyyeh/memryx/mx_examples/multi_dfp_application/cartoonizer_pose/models/YOLO_v8_small_pose_640_640_3_onnx_post.onnx',
                        help="Path to the ONNX post-processing model for Pose Estimation")
    return parser.parse_args()

def shared_capture_loop(src, queue1, queue2, stop_flag):
    cap = cv.VideoCapture(src)
    # Check if the source is a camera or video file
    is_cam = isinstance(src, int) or (isinstance(src, str) and src.startswith('/dev/video'))

    while not stop_flag.is_set():
        ret, frame = cap.read()
        if not ret:
            break
        for q in [queue1, queue2]:
            q.put(frame.copy())
            
    cap.release()

    if not is_cam:
        stop_flag.set()


def main():
    args = parse_args()

    displayer = None
    if args.show:
        app = QApplication(sys.argv)
        displayer = Displayer(num_windows=2)
        displayer.show()

    if args.cam:
        input_source = 0 # default camera
        src_is_cam = True
    elif args.video:
        input_source = args.video
        src_is_cam = False
    else:
        print(" Please specify either --cam or --video <path>")
        sys.exit(1)

    queue_cartoonizer = Queue(maxsize=30)
    queue_pose = Queue(maxsize=30)
    stop_flag = Event()

    capture_proc = Process(
        target=shared_capture_loop,
        args=(input_source, queue_cartoonizer, queue_pose, stop_flag)
    )

    # dfp_cartoon = args.dfp_cartoon
    # dfp_pose = args.dfp_pose
    # pose_post_model = args.post
    # accl = run_cartoonizer(queue_cartoonizer, dfp_cartoon, displayer.update_left,
    #                        src_is_cam, args.frame_limit, stop_flag)

    # accl2 = run_pose_estimation(queue_pose, dfp_pose, pose_post_model, (640, 640),
    #                             displayer.update_right, src_is_cam, args.frame_limit, stop_flag)

    ##### Init Applications #####
    # Cartoonizer(queue, accl, display_thread, src_is_cam=src_is_cam, stop_flag=stop_flag)
    app_car = Cartoonizer( queue_cartoonizer, displayer, src_is_cam=src_is_cam, stop_flag=stop_flag)

    # PoseEstimation(queue, accl, display_thread, input_shape, mirror=True, src_is_cam=src_is_cam, stop_flag=stop_flag, post_model=pose_post_model)
    app_pose = PoseEstimation( queue_pose, displayer, src_is_cam=src_is_cam, stop_flag=stop_flag)


    if args.old_bind:
        print ("Run with old binding")
        import memryx
        from memryx.runtime import SchedulerOptions, ClientOptions

        # Setup SchedulerOptions
        sche_opts = SchedulerOptions()
        sche_opts.frame_limit = args.frame_limit
        sche_opts.ifmap_queue_size = 22
        sche_opts.ofmap_queue_size = 30
        
        ##### Initialize Accl #####
        accl_cartoonizer = memryx.AsyncAccl(args.dfp_cartoon, scheduler_options=sche_opts)
        accl_pose = memryx.AsyncAccl(args.dfp_pose, scheduler_options=sche_opts)
        accl_pose.set_postprocessing_model(args.post_model, model_idx=0)


        ##### Set input and output callbacks #####
        accl_cartoonizer.connect_input(app_car.in_callback_old_bind)
        accl_cartoonizer.connect_output(app_car.out_callback_old_bind)
        
        accl_pose.connect_input(app_pose.in_callback_old_bind)
        accl_pose.connect_output(app_pose.out_callback_old_bind)

        print("[MAIN] Starting capture process and inference threads...")
        capture_proc.start()
        
        ##### Start the accl processes #####
        if args.show:
            sys.exit(app.exec_())
            
        accl_cartoonizer.wait()
        accl_pose.wait()
    else:
        print ("Run with new binding")
        import mxapi
        
        # Setup SchedulerOptions
        sche_opts = mxapi.SchedulerOptions()
        sche_opts.frame_limit = args.frame_limit
        sche_opts.ifmap_queue_size = 22
        sche_opts.ofmap_queue_size = 30

        ##### Initialize Accl #####
        use_model_shape_pair = [True, True]
        local_mode = False
        accl_cartoonizer = mxapi.MxAccl(args.dfp_cartoon, [0], use_model_shape_pair, local_mode, sche_opts)
        accl_pose = mxapi.MxAccl(args.dfp_pose, [0], use_model_shape_pair, local_mode, sche_opts)
        accl_pose.connect_post_model(args.post_model)

        ##### Set input and output callbacks #####
        # Stream ID is a unique identifier for each stream
        stream_ids = [0, 1]
        accl_cartoonizer.connect_stream(app_car.in_callback, app_car.out_callback, stream_ids[0])
        accl_pose.connect_stream(app_pose.in_callback, app_pose.out_callback, stream_ids[1])

        accl_cartoonizer.start()
        accl_pose.start()
        
        print("[MAIN] Starting capture process and inference threads...")
        capture_proc.start()
        
        ##### Start the accl processes #####
        if args.show:
            sys.exit(app.exec_())
            
        accl_cartoonizer.wait()
        accl_pose.wait()


if __name__ == '__main__':
    main()
