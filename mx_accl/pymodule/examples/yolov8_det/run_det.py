import time
import argparse
import numpy as np
import cv2
from queue import Queue, Full
import queue
from threading import Thread
from yolov8 import YoloV8 as YoloModel
from collections import defaultdict
import sys

FPS_LOG_INTERVAL = 30  # print out FPS every X frames

class Yolo8sMxa:
    """
    A demo app to run YOLOv8s on the MemryX MXA.
    """

###################################################################################################
    def __init__(self, video_paths, model_type, use_model_shape_in, use_model_shape_out, show=True):
        """
        Initialization function.
        """
        # Display control and stream initialization
        self.use_model_shape_in = use_model_shape_in
        self.use_model_shape_out = use_model_shape_out
        self.show = show
        self.done = False
        self.num_streams = len(video_paths)  # Number of streams

        # Stream-related containers and initialization
        self.streams = []
        self.streams_idx = [True] * self.num_streams
        self.stream_window = [False] * self.num_streams
        self.cap_queue = {i: Queue(maxsize=50) for i in range(self.num_streams)}
        self.dets_queue = {i: Queue(maxsize=50) for i in range(self.num_streams)}
        self.outputs = {i: [] for i in range(self.num_streams)}
        self.dims = {}
        self.color_wheel = {}
        self.model = {}
        self.model_type = model_type

        # Timing and FPS related
        self.dt_index = {i: 0 for i in range(self.num_streams)}
        self.frame_end_time = {i: 0 for i in range(self.num_streams)}
        self.fps = {i: 0 for i in range(self.num_streams)}
        self.dt_array = {i: np.zeros(30) for i in range(self.num_streams)}
        self.writer = {i: None for i in range(self.num_streams)}
        self.srcs_are_cams = {i: True for i in range(self.num_streams)}
        self.frame_count = {i: 0 for i in range(self.num_streams)}

        # FPS calculation related
        self.frame_count = defaultdict(int)
        self.start_ms = defaultdict(int)
        self.fps_number = defaultdict(float)
        self.history_fps = defaultdict(list)

        # Initialize video captures, models, and dimensions for each stream
        for i, video_path in enumerate(video_paths):
            if "/dev/video" in video_path:
                self.srcs_are_cams[i] = True
            else:
                self.srcs_are_cams[i] = False

            vidcap = cv2.VideoCapture(video_path)
            self.streams.append(vidcap)

            # Get frame dimensions
            self.dims[i] = (int(vidcap.get(cv2.CAP_PROP_FRAME_WIDTH)),
                            int(vidcap.get(cv2.CAP_PROP_FRAME_HEIGHT)))
            self.color_wheel[i] = np.random.randint(0, 255, (20, 3)).astype(np.int32)

            # Initialize the YOLOv8 model
            self.model[i] = YoloModel(stream_img_size=(self.dims[i][1], self.dims[i][0], 3), model_type=self.model_type)


        # Start display thread
        self.display_thread = Thread(target=self.display)

    ###################################################################################################
    def in_callback(self, stream_idx):
        """
        Captures a frame for the video device and pre-processes it.
        """
        while True:
            got_frame, frame = self.streams[stream_idx].read()

            if not got_frame or self.done:
                self.streams_idx[stream_idx] = False
                return None

            if self.srcs_are_cams[stream_idx] and self.cap_queue[stream_idx].full():
                # drop frame
                continue
            else:
                # Put the frame in the cap_queue to be processed later
                try:
                    self.cap_queue[stream_idx].put(frame, timeout=2)
                except Full:
                    print('Dropped frame')
                    continue
                    
                # Pre-process the frame using the corresponding model
                frame = self.model[stream_idx].preprocess(frame)
                
                if not self.use_model_shape_in:
                    frame = np.transpose(frame, (2, 3, 0, 1))

                # print (f"Stream {stream_idx}, input frame.shape: {frame.shape}")
                # print(frame.flags['C_CONTIGUOUS'])  # True if C-contiguous

                return frame


    ###################################################################################################
    def out_callback(self, mxa_output, stream_idx):
        """
        Post-process the output from MXA.
        """
        # print (f"Stream {stream_idx}, out frame.shape: {mxa_output[0].shape}")
        dets = self.model[stream_idx].postprocess(mxa_output)  # Get detection results

        # Queue detection results for display
        self.dets_queue[stream_idx].put(dets)

        # Calculate FPS
        self.update_fps(stream_idx)

    # for old binding test
    def in_callback_old_bind(self):
        stream_id = 0 # dummy
        return self.in_callback(stream_id)
    
    # for old binding test
    def out_callback_old_bind(self, *ofmaps):
        self.out_callback(list(ofmaps), stream_idx=0)

    # for old binding test
    def in_callback_old_bind_multistream(self, stream_id):
        return self.in_callback(stream_id)
    
    # for old binding test
    def out_callback_old_bind_multistream(self, stream_id, *ofmaps):
        self.out_callback(list(ofmaps), stream_id)
        
    def update_fps(self, stream_idx):

        # increment frame count
        self.frame_count[stream_idx] += 1
        
        now_ms = int(time.time() * 1000)

        if self.frame_count[stream_idx] == 1:
            # record start time
            self.start_ms[stream_idx] = now_ms
        else:
            # print FPS
            if self.frame_count[stream_idx] % FPS_LOG_INTERVAL == 0:
                
                # msg
                msg = "Frame cnt: {} stream {} => FPS: {:.2f}"
                lines = [msg.format(self.frame_count[i], i, self.fps_number[i]) for i in range(self.num_streams)]
                print("\n".join(lines))

                # Overwrite previous msg
                sys.stdout.write(f"\033[{self.num_streams}A")
                sys.stdout.flush()

                # Update history
                for i in range(self.num_streams):
                    self.history_fps[i].append(self.fps_number[i])

            # update fps_number
            duration_ms = now_ms - self.start_ms[stream_idx]
            self.fps_number[stream_idx] = (self.frame_count[stream_idx] * 1000.0) / duration_ms

    def get_avg_fps(self, stream_idx):
        if self.history_fps[stream_idx]:
            return np.mean(self.history_fps[stream_idx])
        return 0

###################################################################################################
    def display(self):
        """
        Displays the processed frames with detections in separate windows.
        """
        while not self.done:
            # Iterate through each stream for displaying frames
            for stream_idx in range(self.num_streams):
                
                try:
                    # Python blocky queue, no need to check if not queue.empty()
                    frame = self.cap_queue[stream_idx].get(timeout=2)
                    dets = self.dets_queue[stream_idx].get(timeout=2)
                except queue.Empty:
                    break 

                self.cap_queue[stream_idx].task_done()
                self.dets_queue[stream_idx].task_done()

                # Draw detection boxes
                for d in dets:
                    x1, y1, w, h = d['bbox']
                    color = tuple(int(c) for c in self.color_wheel[stream_idx][d['class_id'] % 20])

                    # Draw bounding boxes
                    frame = cv2.rectangle(frame, (int(x1), int(y1)), (int(x1 + w), int(y1 + h)), color, 2)

                    # Add class label
                    frame = cv2.putText(frame, d['class'], (x1 + 2, y1 - 5),
                                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 2)

                # Add FPS to frame
                fps_text = f"{self.fps_number[stream_idx]:.2f}"
                frame = cv2.putText(frame, fps_text, (50, 50), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 0, 0), 2)

                if self.show:
                    window_name = f"Stream {stream_idx} - YOLOv8s"
                    cv2.imshow(window_name, frame)

            # Exit if 'q' is pressed
            if cv2.waitKey(1) == ord('q'):
                self.done = True

        # Close all windows and release resources after processing
        cv2.destroyAllWindows()
        for stream in self.streams:
            stream.release()

###################################################################################################


def run(args):

    if args.post_model.endswith('.onnx'):
        model_type = 'onnx'
    elif args.post_model.endswith('.tflite'):
        model_type = 'tflite'
    else:
        raise ValueError(f"Unsupported post-processing model format: {args.post_model}")

    use_model_shape_pair = [args.use_model_shape_in, args.use_model_shape_out]

    # Initialize the application with video paths and display settings
    app = Yolo8sMxa(
        video_paths=args.video_paths,
        model_type=model_type,
        use_model_shape_in=args.use_model_shape_in,
        use_model_shape_out=args.use_model_shape_out,
        show=args.show,
    )

    num_streams = len(args.video_paths)
    
    app.display_thread.start()  # Start the display thread

    if args.old_bind:
        print ("Run with old binding")
        import memryx
        
        if num_streams == 1:    
            accl = memryx.AsyncAccl(args.dfp_path, [0], use_model_shape_pair, args.local)
            accl.set_postprocessing_model(args.post_model)
            accl.connect_input(app.in_callback_old_bind)
            accl.connect_output(app.out_callback_old_bind)
        else:
            stream_workers = num_streams
            accl = memryx.MultiStreamAsyncAccl(args.dfp_path, [0], stream_workers, use_model_shape_pair, args.local)
            accl.set_postprocessing_model(args.post_model)
            accl.connect_streams(app.in_callback_old_bind_multistream, app.out_callback_old_bind_multistream, num_streams)

        accl.wait()

    else:
        print ("Run with new binding")
        import mxapi
        accl = mxapi.MxAccl(args.dfp_path, [0], use_model_shape_pair, args.local)
        accl.connect_post_model(args.post_model)

        for i in range(num_streams):
            accl.connect_stream(app.in_callback, app.out_callback, i)

        accl.start()
        accl.wait()

    # Join display thread
    app.done = True

    app.display_thread.join()

###################################################################################################

if __name__ == "__main__":
    # Argument parser
    parser = argparse.ArgumentParser(description="\033[34mMemryX YoloV8s Demo\033[0m")
    
    # Video input paths
    parser.add_argument('--video_paths', nargs='+', dest="video_paths", 
                        action="store", 
                        default=['/dev/video0'],
                        help="Path to video files for inference. Use '/dev/video0' for webcam. (Default:'/dev/video0')")
    
    parser.add_argument("--use_model_shape_in", action="store_true", help="use model input shape")
    parser.add_argument("--use_model_shape_out", action="store_true", help="use model output shape")
    parser.add_argument("--old_bind", action="store_true", help="Use old binding method")
    parser.add_argument("--local", action="store_true", help="Use local mode")

    # Option to turn off display
    parser.add_argument('--show', 
                        action="store_true", 
                        help="Optionally turn off the video display")

    # DFP model argument
    parser.add_argument('-d', '--dfp_path', type=str, 
                        default='../../models/tflite/YOLO_v8_small_640_640_3_tflite.dfp', 
                        help="Path to the compiled DFP file (default: 'models/tflite/YOLO_v8_small_640_640_3_tflite.dfp')")

    # Post-processing model argument
    parser.add_argument('-p', '--post_model', type=str, 
                        default='../../models/tflite/YOLO_v8_small_640_640_3_tflite_post.tflite', 
                        help="Path to the post-processing ONNX file (default: 'models/tflite/YOLO_v8_small_640_640_3_tflite_post.tflite')")

    args = parser.parse_args()

    # Call the main function
    run(args)

# eof
