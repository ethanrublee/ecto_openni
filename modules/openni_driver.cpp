/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011 2011 Willow Garage, Inc.
 *    Suat Gedikli <gedikli@willowgarage.com>
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <ecto/ecto.hpp>
#include <openni_wrapper/openni_driver.h>
#include <openni_wrapper/openni_device.h>
#include <openni_wrapper/openni_image.h>
#include <openni_wrapper/openni_depth_image.h>
#include <openni_wrapper/openni_ir_image.h>
#include <openni_wrapper/synchronizer.h>
#include <iostream>
#include <string>
#include <map>
#include <XnCppWrapper.h>
#include <opencv2/opencv.hpp>
#include <boost/thread.hpp>
#include <sys/times.h>

#include "enums.hpp"

using namespace std;
using namespace openni_wrapper;
using namespace cv;
using namespace boost;

class OpenNIStuff
{
public:
  OpenNIStuff(unsigned device_index, int width, int height, int nFPS, bool registration);
  void
  start(ecto_openni::StreamMode mode);
  void
  getLatest(ecto_openni::StreamMode mode, cv::Mat& depth, cv::Mat& image, cv::Mat& ir);
  void
  dataReady(ecto_openni::StreamMode mode);
private:
  void
  imageCallback(boost::shared_ptr<Image> image, void* cookie);
  void
  irCallback(boost::shared_ptr<IRImage> image, void* cookie);
  void
  depthCallback(boost::shared_ptr<DepthImage> depth, void* cookie);
  void
  syncRGBCallback(const boost::shared_ptr<openni_wrapper::Image> &image,
                  const boost::shared_ptr<openni_wrapper::DepthImage> &depth_image);
  void
  syncIRCallback(const boost::shared_ptr<openni_wrapper::IRImage> &image,
                 const boost::shared_ptr<openni_wrapper::DepthImage> &depth_image);
  typedef map<string, Mat> DeviceImageMapT;
  DeviceImageMapT rgb_images_;
  DeviceImageMapT gray_images_;
  DeviceImageMapT ir_images_;
  DeviceImageMapT depth_images_;
  vector<boost::shared_ptr<OpenNIDevice> > devices_;
  unsigned selected_device_;
  boost::condition_variable cond;
  boost::mutex mut;
  int data_ready;
  ecto_openni::StreamMode stream_mode_;
};

OpenNIStuff::OpenNIStuff(unsigned device_index, int width, int height, int nFPS, bool registration)
    :
      selected_device_(0)
{
  OpenNIDriver& driver = OpenNIDriver::getInstance();

  if (device_index >= driver.getNumberDevices())
  {
    std::stringstream ss;
    ss << "Index out of range." << driver.getNumberDevices() << " devices found.";
    throw std::runtime_error(ss.str());
  }

  boost::shared_ptr<OpenNIDevice> device = driver.getDeviceByIndex(device_index);
  cout << devices_.size() + 1 << ". device on bus: " << (int) device->getBus() << " @ " << (int) device->getAddress()
       << " with serial number: " << device->getSerialNumber() << "  " << device->getVendorName() << " : "
       << device->getProductName() << endl;
  selected_device_ = devices_.size();
  devices_.push_back(device);
  XnMapOutputMode mode;
  mode.nXRes = width;
  mode.nYRes = height;
  mode.nFPS = nFPS;

  if (device->hasImageStream())
  {
    if (!device->isImageModeSupported(mode))
    {
      std::stringstream ss;
      ss << "image stream mode " << mode.nXRes << " x " << mode.nYRes << " @ " << mode.nFPS << " not supported";
      throw std::runtime_error(ss.str());
    }
    rgb_images_[device->getConnectionString()] = Mat::zeros(height, width, CV_8UC3);
    gray_images_[device->getConnectionString()] = Mat::zeros(height, width, CV_8UC1);
    device->registerImageCallback(&OpenNIStuff::imageCallback, *this, &(*device));
  }

  if (device->hasIRStream())
  {
    if (!device->isImageModeSupported(mode))
    {
      std::stringstream ss;
      ss << "IR stream mode " << mode.nXRes << " x " << mode.nYRes << " @ " << mode.nFPS << " not supported";
      throw std::runtime_error(ss.str());
    }
    ir_images_[device->getConnectionString()] = Mat::zeros(height, width, CV_16UC1);
    device->registerIRCallback(&OpenNIStuff::irCallback, *this, &(*device));
  }

  if (device->hasDepthStream())
  {
    if (!device->isDepthModeSupported(mode))
    {
      std::stringstream ss;

      ss << "depth stream mode " << mode.nXRes << " x " << mode.nYRes << " @ " << mode.nFPS << " not supported" << endl;
      throw std::runtime_error(ss.str());
    }
    if(device->isDepthRegistrationSupported() && registration)
    {
      device->setDepthRegistration(registration);
    }
    depth_images_[device->getConnectionString()] = Mat::zeros(height, width, CV_32FC1);
    device->registerDepthCallback(&OpenNIStuff::depthCallback, *this, &(*device));
  }
}
void
OpenNIStuff::imageCallback(boost::shared_ptr<Image> image, void* cookie)
{
  OpenNIDevice* device = reinterpret_cast<OpenNIDevice*>(cookie);
  Mat rgb = rgb_images_[device->getConnectionString()];

  unsigned char* buffer = (unsigned char*) (rgb.data);
  image->fillRGB(rgb.cols, rgb.rows, buffer, rgb.step);

  dataReady(ecto_openni::RGB);
}

void
OpenNIStuff::irCallback(boost::shared_ptr<IRImage> image, void* cookie)
{
  OpenNIDevice* device = reinterpret_cast<OpenNIDevice*>(cookie);
  Mat ir = ir_images_[device->getConnectionString()];

  uint16_t* buffer = (uint16_t*) (ir.data);
  image->fillRaw(ir.cols, ir.rows, buffer, ir.step);
  dataReady(ecto_openni::IR);
}

void
OpenNIStuff::depthCallback(boost::shared_ptr<DepthImage> image, void* cookie)
{
  OpenNIDevice* device = reinterpret_cast<OpenNIDevice*>(cookie);
  Mat depth = depth_images_[device->getConnectionString()];

  float* buffer = (float*) depth.data;
  image->fillDepthImage(depth.cols, depth.rows, buffer, depth.step);
  dataReady(ecto_openni::DEPTH);
}

void
OpenNIStuff::dataReady(ecto_openni::StreamMode mode)
{
  {
    boost::lock_guard<boost::mutex> lock(mut);
    data_ready |= mode;
  }
  cond.notify_one();
}

void
OpenNIStuff::getLatest(ecto_openni::StreamMode mode, cv::Mat& depth, cv::Mat& image, cv::Mat& ir)
{
  std::string connection = devices_[selected_device_]->getConnectionString();
  if (stream_mode_ != mode)
  {
    start(mode);
  }

  boost::unique_lock<boost::mutex> lock(mut);
  while (data_ready != mode)
  {
    cond.wait(lock);
  }

  if (mode & ecto_openni::IR)
  {
    Mat ir_ = ir_images_[connection];
    ir_.copyTo(ir);
  }
  if (mode & ecto_openni::DEPTH)
  {
    Mat depth_ = depth_images_[connection];
    depth_.copyTo(depth);
  }
  if (mode & ecto_openni::RGB)
  {
    Mat image_ = rgb_images_[connection];
    image_.copyTo(image);
  }
  data_ready = 0;
}

void
OpenNIStuff::start(ecto_openni::StreamMode mode)
{
  devices_[selected_device_]->stopDepthStream();
  devices_[selected_device_]->stopIRStream();
  devices_[selected_device_]->stopImageStream();
  if (mode & ecto_openni::IR)
  {
    devices_[selected_device_]->startIRStream();
  }
  if (mode & ecto_openni::DEPTH)
  {
    devices_[selected_device_]->startDepthStream();
  }
  if (mode & ecto_openni::RGB)
  {
    devices_[selected_device_]->startImageStream();
  }
  stream_mode_ = mode;
  data_ready = 0;
}
namespace ecto_openni
{
  using ecto::tendrils;
  struct OpenNICapture
  {

    static void
    declare_params(tendrils& p)
    {
      p.declare(&OpenNICapture::stream_mode_, "stream_mode", "The stream mode to capture. This is dynamic.");
    }

    static void
    declare_io(const tendrils& p, tendrils& i, tendrils& o)
    {
      o.declare(&OpenNICapture::depth_, "depth", "The depth stream.");
      o.declare(&OpenNICapture::image_, "image", "The image stream.");
      o.declare(&OpenNICapture::ir_, "ir", "The IR stream.");
    }

    void
    configure(const tendrils& p, const tendrils& i, const tendrils& o)
    {
      connect(0, 640, 480, 30);
    }

    void
    connect(unsigned device_index, int width, int height, int nFPS)
    {
      device_.reset(new OpenNIStuff(device_index, width, height, nFPS,true));
    }

    int
    process(const tendrils&, const tendrils&)
    {
      //realloc everyframe to avoid threading issues.
      cv::Mat depth, image, ir;
      device_->getLatest(*stream_mode_, depth, image, ir);
      *depth_ = depth;
      *ir_ = ir;
      if (!image.empty())
        cv::cvtColor(image, image, CV_RGB2BGR);
      *image_ = image;
      return ecto::OK;
    }
    ecto::spore<StreamMode> stream_mode_;
    ecto::spore<cv::Mat> depth_, ir_, image_;
    boost::shared_ptr<OpenNIStuff> device_;
  }
  ;
}

ECTO_CELL(ecto_openni, ecto_openni::OpenNICapture, "OpenNICapture", "Raw data capture off of an OpenNI device.");
