package com.dkmp.plugin;

import org.apache.cordova.CallbackContext;
import org.apache.cordova.CordovaPlugin;
import org.json.JSONArray;
import org.json.JSONException;
import org.libsdl.app.SDLActivity;

import android.content.Intent;

import com.dkmp.activity.BroadcastActivity;
public class RTVideo extends CordovaPlugin{
	 @Override
	 public boolean execute(String action, JSONArray args, CallbackContext callbackContext) throws JSONException {
	    if("play".equals(action)) {
	    	final String videourl = args.getString(0);
	    	final String para = args.getString(1);
	    	cordova.getActivity().runOnUiThread(new Runnable( ){
				@Override
				public void run() {
					Intent intent = new Intent(webView.getContext(), SDLActivity.class);
					intent.putExtra("videourl", videourl);
					intent.putExtra("para", para);
					cordova.getActivity().startActivity(intent);
					cordova.getActivity().overridePendingTransition(0, 0);
				}
			});
	    }
	    else if("broadcast".equals(action)){
	    	Intent intent = new Intent(webView.getContext(), BroadcastActivity.class);
	    	intent.putExtra("serviceIp", args.getString(0));
	    	intent.putExtra("para", args.getString(1));
	    	webView.getContext().startActivity(intent);
	    }
		return true;
	}
}
