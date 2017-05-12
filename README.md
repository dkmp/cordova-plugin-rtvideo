# cordova-plugin-rtvideo
cordova ffmpeg sdl2 h264 aac rtsp

# usage
var paraObj = {};
paraObj.enableTcp=true;
paraObj.failRetryInterval = 3000;
paraObj.failRetryTimes =5;
video.play("rtsp://218.204.223.237:554/live/1/66251FC11353191F/e7ooqwcfbqjoo80j.sdp",paraObj);
