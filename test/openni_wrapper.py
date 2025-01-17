#!/usr/bin/env python
import ecto
from ecto_opencv.highgui import imshow, NiConverter, FPSDrawer
from ecto_opencv.imgproc import ConvertTo
from ecto_opencv.cv_bp import CV_8UC1
from ecto_openni import OpenNICapture, DEPTH_RGB, DEPTH_IR
device = 0
capture = OpenNICapture(stream_mode=DEPTH_RGB, registration=False)
next_mode = DEPTH_IR
fps = FPSDrawer('fps')
conversion = ConvertTo(cv_type=CV_8UC1, alpha=0.5) # scale the IR so that we can visualize it.
plasm = ecto.Plasm()
plasm.insert(capture)
plasm.connect(capture['image'] >> fps[:],
              fps[:] >> imshow('image display', name='image')[:],
              capture['depth'] >> imshow('depth display', name='depth')[:],
              capture['ir'] >> conversion[:],
              conversion[:] >> imshow('IR display', name='IR')[:],
              )

sched = ecto.schedulers.Singlethreaded(plasm)
while True:
    sched.execute(niter=100)
    #swap it modes
    next_mode, capture.params.stream_mode = capture.params.stream_mode, next_mode

