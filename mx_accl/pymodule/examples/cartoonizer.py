import argparse
from dataclasses import dataclass
import time
import sys
import numpy as np
import cv2 as cv

AVG_FPS_CALC_FRAME_COUNT = 50
stop_flag = False

class Cartoonizer:
    def __init__(self, cam, stop_flag, use_model_shape_in, use_model_shape_out, show=True, scale=1.0):
        self.cam = cam
        self.stop_flag = stop_flag
        self.use_model_shape_in = use_model_shape_in
        self.use_model_shape_out = use_model_shape_out
        self.show = show
        self.scale = scale

        self.input_height = None
        self.input_width = None

        # fps
        self.frame_count = 0
        self.total_frame_count = 0
        self.start_ms = None
        self.fps_number = 0.0
        self.history_fps = []

    def in_callback(self, stream_id):

        if self.stop_flag:
            return None

        while True:
            ok, frame = self.cam.read()
            if not ok:
                print("EOF")
                return None

            if self.input_height is None or self.input_width is None:
                self.input_height = int(frame.shape[0] * self.scale)
                self.input_width = int(frame.shape[1] * self.scale)
                
            # resize to model size
            img = cv.resize(frame, (512, 512)).astype(np.float32)

            # normalization
            img = img / 127.5 - 1

            if self.use_model_shape_in:
                # [512, 512, 3] -> [1, 3, 512, 512]
                img = np.expand_dims(img, 0)
                img = np.transpose(img, (0, 3, 1, 2))
            else:
                # [512, 512, 3] -> [512, 512, 1, 3]
                img = np.expand_dims(img, 2)
            # print("[CARTOONIZER IN] iframe shape:", img.shape)
            
            return img
    
    def out_callback(self, ofmaps, stream_id):
        
        # Cartoonizer contains only a single ofmap
        ofmap = ofmaps[0]
        
        # TODO: use ofmaps instead of ofmap
        # print("[CARTOONIZER OUT] ofmap shape:", ofmap.shape)

        if self.use_model_shape_out:
            # [1, 3, 512, 512] -> [512, 512, 3]
            ofmap = np.squeeze(ofmap, axis=0)
            ofmap = np.transpose(ofmap, (1, 2, 0))
        else:
            # [512, 512, 1, 3] -> [512, 512, 3]
            ofmap = np.squeeze(ofmap, axis=2)
            
        # add +1.0 to all output values and convert to UINT8
        ofmap = (ofmap + 1) * 127.5
        ofmap = np.clip(ofmap, 0, 255).astype(np.uint8)

        # resize
        display_img = cv.resize(ofmap, (self.input_width, self.input_height))


        self.display(display_img)

        self.update_fps()

    # for old binding test
    def in_callback_old_bind(self):
        stream_id = 0 # dummy
        return self.in_callback(stream_id)
    
    # for old binding test
    def out_callback_old_bind(self, *ofmaps):
        self.out_callback(list(ofmaps), stream_id=0)
        
    def display(self, img):
        if not self.show:
            return
        cv.imshow("Facial cartoonizer demo", img)
        if cv.waitKey(1) == ord("q"):
            self._free(self.cam, self.writer)
            exit(1)

    def update_fps(self):

        self.frame_count += 1
        self.total_frame_count += 1

        if self.frame_count == 1:
            self.start_ms = time.time() * 1000  # milliseconds
        elif self.frame_count % AVG_FPS_CALC_FRAME_COUNT == 0:
            now_ms = time.time() * 1000
            duration = now_ms - self.start_ms
            self.fps_number = AVG_FPS_CALC_FRAME_COUNT * 1000 / duration
            self.frame_count = 0
            print(f"Frame count: {self.total_frame_count} FPS: {self.fps_number:<.2f}")
            self.history_fps.append(self.fps_number)


def run_app(args):
    
    cam = cv.VideoCapture(args.input_source)
    use_model_shape_pair = [args.use_model_shape_in, args.use_model_shape_out]
    
    # init app
    app = Cartoonizer(cam, stop_flag, args.use_model_shape_in, args.use_model_shape_out, args.show)
    
    if args.old_bind:
        print ("Run with old binding")
        # from memryx import AsyncAccl   ## this way cannot work
        import memryx
        accl = memryx.AsyncAccl(args.dfp_path, [0], use_model_shape_pair, args.local)

        accl.connect_input(app.in_callback_old_bind)
        accl.connect_output(app.out_callback_old_bind)
        accl.wait()

    else:
        print ("Run with new binding")
        import mxapi # this import should not be included when args.old_bind is True
        accl = mxapi.MxAccl(args.dfp_path, [0], use_model_shape_pair, args.local)
        
        stream_id = 0
        accl.connect_stream(app.in_callback, app.out_callback, stream_id)

        accl.start()
        accl.wait()

    # print final FPS
    print("\r\033[K", end="")   # clear line
    print(f'Final Avg FPS: {np.mean(app.history_fps):.2f}')

if __name__ == "__main__":

    # parser for dependent arguments
    parser = argparse.ArgumentParser()
    parser.add_argument("--use_model_shape_in", action="store_true", help="use model input shape")
    parser.add_argument("--use_model_shape_out", action="store_true", help="use model output shape")
    parser.add_argument("-d", "--dfp_path", type=str, default="./models/cartoonizer/cartoonizer.dfp", help="path to the dfp file")

    parser.add_argument("--show", action="store_true", help="display output result")
    parser.add_argument("--cam", action="store_true", help="Use webcam instead of video")
    parser.add_argument("--old_bind", action="store_true", help="Use old binding method")
    parser.add_argument("--local", action="store_true", help="Use local mode")
    parser.add_argument("--video", type=str, help="Path to video file")

    args = parser.parse_args()

    # Set up input source
    if args.cam:
        input_source = "/dev/video0"
    elif args.video:
        input_source = args.video
    else:
        print(" Please specify either --cam or --video <video_path>")
        sys.exit(1)

    args.input_source = input_source

    # run
    run_app(args)
