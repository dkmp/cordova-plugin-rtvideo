package com.dkmp.activity;

import java.io.IOException;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

import android.app.Activity;
import android.content.Intent;
import android.hardware.Camera;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Build;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup.LayoutParams;
import android.widget.ImageButton;
import android.widget.RelativeLayout;
import android.widget.Toast;

public class BroadcastActivity extends Activity implements SurfaceHolder.Callback,OnClickListener {
	private final int BTN_EXIT = 0;
	private Lock lock = new ReentrantLock();
	private String mSdpName = "channel_vlc.sdp";
	private boolean mEnableTcp = false;

	private boolean mEnableAudio = true;
	private int mAudioBitRate = 64000;
	private final int mAudioFrequency = 44100;
	private final int mAudioChnCfg = AudioFormat.CHANNEL_IN_MONO;
	private final int mAudioEncode = AudioFormat.ENCODING_PCM_16BIT;
	private int mAudioBufSize;
	private byte[] mAudioBuf;
	
	private Thread mAudioSampleThread;
	private AudioRecord mRecorder; 
	
	private boolean mEnableVideo = true;
	private int mVideoHeight = 480;
	private int mVideoWidth = 640;
	private int mVideoBitRate = 800000;
	private int mVideoGopSize = 30;
	private int mVideoMaxBFrames = 2;
	
	private Camera mCamera;
	private Camera.PreviewCallback mPreviewCallbacx;
	
	private void startAudioSample()
	{
		mAudioSampleThread = new Thread() { 
        	public void run() {
        		Thread thisThread = Thread.currentThread(); 
        		while (mAudioSampleThread == thisThread) { 
        			int size = mRecorder.read(mAudioBuf, 0, 2048);
        			lock.lock();
        			if (AudioRecord.ERROR_INVALID_OPERATION != size) {
            			encodeAudio(mAudioBuf);
            		}
        			lock.unlock();
        		}
        	}
        };
        mRecorder.startRecording();
        mAudioSampleThread.start();
	}
	
	private void stopAudioSample()
	{
		mRecorder.stop();
		mAudioSampleThread = null;
	}
	
	private void startCameraSample()
	{
		if(Build.VERSION.SDK_INT>=Build.VERSION_CODES.GINGERBREAD) {
    		mCamera=Camera.open(0);
    	} else {
    		mCamera=Camera.open();
    	}
    	mCamera.setDisplayOrientation(90);
	}
	
	private void stopCameraSample()
	{
		if(mCamera!=null) {
			mCamera.setPreviewCallback(null);
			mCamera.stopPreview();
		    mCamera.release();
		    mCamera = null;
		}
	}
	
	@Override
    protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		loadLibraries();
		
		Intent intent=getIntent();
    	String ip = intent.getExtras().getString("serviceIp");
    	String para = intent.getExtras().getString("para");
    	JSONTokener jsonParser = new JSONTokener(para);
    	JSONObject paraObj;
    	
        try {
        	paraObj = (JSONObject) jsonParser.nextValue();
        	if(paraObj.has("sdpName")) {
        		String sdpName = paraObj.getString("sdpName");
        		if(sdpName.endsWith(".sdp"))
        		{
        			mSdpName = sdpName;
        		}
        		else
        		{
        			mSdpName = sdpName + ".sdp";
        		}
        	}
        	if(paraObj.has("enableTcp")) {
        		mEnableTcp = paraObj.getBoolean("enableTcp");
        	}
        	if(paraObj.has("enableAudio")) {
        		mEnableAudio = paraObj.getBoolean("enableAudio");
        	}
        	if(paraObj.has("audioBitRate")) {
        		mAudioBitRate = paraObj.getInt("audioBitRate");
        	}
        	if(paraObj.has("enableVideo")) {
        		mEnableVideo = paraObj.getBoolean("enableVideo");
        	}
        	if(paraObj.has("videoBitRate")) {
        		mVideoBitRate = paraObj.getInt("videoBitRate");
        	}
        	if(paraObj.has("videoWidth")) {
        		mVideoWidth = paraObj.getInt("videoWidth");
        	}
        	if(paraObj.has("videoHeight")) {
        		mVideoHeight = paraObj.getInt("videoHeight");
        	}
        	if(paraObj.has("videoGopSize")) {
        		mVideoGopSize = paraObj.getInt("videoGopSize");
        	}
        	if(paraObj.has("videoMaxBFrames")) {
        		mVideoMaxBFrames = paraObj.getInt("videoMaxBFrames");
        	}
		} catch (JSONException e) {
			e.printStackTrace();
		}

		if(initial(ip, mSdpName, mEnableTcp, mEnableAudio, mAudioBitRate,
				mEnableVideo, mVideoBitRate, mVideoWidth, mVideoHeight, mVideoGopSize, mVideoMaxBFrames)<0)
		{
			Toast.makeText(getApplicationContext(), "³õÊ¼»¯Ê§°Ü", Toast.LENGTH_SHORT).show();
			this.finish();
		}
		
		mPreviewCallbacx = new Camera.PreviewCallback() {
			@Override
			public void onPreviewFrame(byte[] arg0, Camera arg1) {
				if(mEnableVideo)
				{
					lock.lock();
					encodeVideo(arg0);
					lock.unlock();
				}
			}
		};
		
		RelativeLayout layoutMain = new RelativeLayout(this);
		RelativeLayout.LayoutParams layoutMainLp = new RelativeLayout.LayoutParams(LayoutParams.MATCH_PARENT,LayoutParams.MATCH_PARENT);
		layoutMain.setLayoutParams(layoutMainLp);
		
		SurfaceView surfaceCamera = new SurfaceView(this);
		SurfaceHolder holder = surfaceCamera.getHolder();
        holder.addCallback(this);
        
        mAudioBufSize = AudioRecord.getMinBufferSize(mAudioFrequency, mAudioChnCfg, mAudioEncode);
        mRecorder = new AudioRecord(MediaRecorder.AudioSource.MIC,mAudioFrequency,mAudioChnCfg,mAudioEncode,mAudioBufSize);
        mAudioBuf = new byte[mAudioBufSize];
        
        RelativeLayout.LayoutParams btnExitLp = new RelativeLayout.LayoutParams(150,150);
        btnExitLp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT, RelativeLayout.TRUE);
        
        ImageButton btnExit = new ImageButton(getApplicationContext()); 
        btnExit.setBackgroundResource(getResources().getIdentifier("sfp_video_btn_exit", "drawable", getPackageName()));
        btnExit.setId(BTN_EXIT);
        btnExit.setLayoutParams(btnExitLp);
        btnExit.setOnClickListener(this);
        
        layoutMain.addView(surfaceCamera);
        layoutMain.addView(btnExit);
        
        setContentView(layoutMain);
	}
		
	@Override
	public void onClick(View v) {
		int id = v.getId();
		if(id == BTN_EXIT) {
			if(mEnableAudio)
			{
				stopAudioSample();
				closeAudio();
			}
			if(mEnableVideo)
			{
				stopCameraSample();
				closeVideo();
			}
		    close();
			this.finish();
		}
	}

	@Override
	public void surfaceCreated(SurfaceHolder arg0) {
		try {
			if(mCamera!=null) {
				mCamera.setPreviewDisplay(arg0);
			}
		}catch(IOException exception){
			
		}
	}
	 
	@Override
	public void surfaceChanged(SurfaceHolder arg0, int arg1, int arg2, int arg3) {
		if(mCamera==null) return;
		Camera.Parameters parameters=mCamera.getParameters();			
		parameters.setPreviewSize(mVideoWidth, mVideoHeight);
		parameters.setPictureSize(mVideoWidth, mVideoHeight);
		parameters.setFocusMode(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE);
		mCamera.setParameters(parameters);
		try{
			mCamera.setPreviewDisplay(arg0);
			mCamera.startPreview();
		}catch(Exception e){

		}
		mCamera.setPreviewCallback(mPreviewCallbacx);
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder arg0) {
		
	}
	
	@Override
    protected void onResume(){
    	super.onResume();
    	startCameraSample();
    	if(mEnableAudio)
        {
    		startAudioSample();
        }
    }
    
    @Override
    protected void onPause() {
    	super.onPause();
    	if(mEnableVideo)
    	{
    		stopCameraSample();
    	}
    	if(mEnableAudio)
		{
    		stopAudioSample();
		}
    }

	
	private String[] getLibraries() {
        return new String[] {
    		 "SDL2",
             "avcodec-57",
             "avdevice-57",
             "avfilter-6",
             "avformat-57",
             "avutil-55",
             "postproc-54",
             "swresample-2",
             "swscale-4",
             "x264",
             "fdk-aac",
             "sdlvideo"
        };
    }

    public void loadLibraries() {
       for (String lib : getLibraries()) {
          System.loadLibrary(lib);
       }
    }
    
    public native int initial(String ip, String sdpName, boolean bUdp, boolean enableAudio, int audioBitRate, boolean enableVideo, int videoBitRate, int videoWidth,int videoHeight, int videoGopSize, int videoMaxBFrames);
    public native int encodeAudio(byte[] audioData);
    public native int encodeVideo(byte[] videoData);
    public native int closeAudio();
    public native int closeVideo();
    public native int close();
}
