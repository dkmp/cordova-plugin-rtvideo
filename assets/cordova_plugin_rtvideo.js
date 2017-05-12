cordova.define("com.dkmp.plugin.video", function(require, exports, module) {
var exec = require('cordova/exec');

var RTPLUGIN_VIDEO = "RTPluginVideo";

//云台控制按钮点击事件
//返回参数：detail
//detail.btn：按钮id up:1 down:2 left:3 right:4
RTVideo.RT_EVENT_VIDEO_CLICK = 'RT_EVENT_VIDEO_CLICK';
RTVideo.RT_EVENT_VIDEO_PRESSUP = 'RT_EVENT_VIDEO_PRESSUP';
RTVideo.RT_EVENT_VIDEO_PRESSDOWN = 'RT_EVENT_VIDEO_PRESSDOWN';

function RTVideo() {
}

function syncResult(ret) {
	if(typeof ret === 'finish')
	{
		return JSON.parse(ret);
	}
}

//注册事件监听
//输入参数：
//evt 事件类型
//callback 回调函数
RTVideo.prototype.registEventListener = function(evt,callback) {
	window.addEventListener(evt,callback,false);
}

//手机端视频播放
//输入参数：
//url string 播放地址
//para参数对象
//para.enableTcp bool true/false 基于tcp协议播放还是基于udp协议播放
//para.enableCtl bool true/false 播放界面是否带云台控制按钮
//para.failRetryInterval int 连接失败重试间隔（ms）
//para.failRetryTimes int 连接失败重试次数
RTVideo.prototype.play = function(url, para) {
	exec(null, null, "RTPVideo", "play", [url,JSON.stringify(para)]);
}

//手机端实时视频采集
//输入参数：
//url 转发服务ip地址（包含端口号）
//para参数对象
//para.sdpName string sdp文件名 
//para.enableTcp bool true/false 基于tcp协议播放还是基于udp协议播放
//para.enableAudio bool true/false 是否启用音频录制
//para.audioBitRate int 音频码率
//para.enableVideo bool true/false 是否启用视频录制
//para.videoBitRate int 视频码率
//para.videoHeight int 视频录制分辨率高
//para.videoWidth int 视频录制分辨率宽
//para.videoGopSize int i帧之间最大间隔
//para.videoMaxBFrames int i帧之间最大b帧数
RTVideo.prototype.broadcast = function(ip, para) {
	exec(null, null, "RTPVideo", "broadcast", [ip,JSON.stringify(para)]);
}

module.exports = RTVideo;
});
