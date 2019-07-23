package com.testapp.h264nativedecoder;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.os.Bundle;
import android.os.Environment;
import android.support.v4.content.ContextCompat;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private final String TAG = "H264 Native Decode";

    private final String MP4_FILE =
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES) + "/r4.mp4";
    private final String DUMP_FILE_DIR =
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES).toString();

    private VideoView mVideoView;
    private RelativeLayout mMainLayout;

    private int videoHeight, videoWidth;
    private boolean mCreated = false, mPlaying = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        mMainLayout = (RelativeLayout)findViewById(R.id.main_layout);

        int read_permission = ContextCompat.checkSelfPermission(this,
                Manifest.permission.READ_EXTERNAL_STORAGE);
        int write_permission =  ContextCompat.checkSelfPermission(this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE);

        List<String> listPermissionsNeeded = new ArrayList<>();

        if (read_permission != PackageManager.PERMISSION_GRANTED)
            listPermissionsNeeded.add(Manifest.permission.READ_EXTERNAL_STORAGE);
        if (write_permission != PackageManager.PERMISSION_GRANTED)
            listPermissionsNeeded.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);

        if (!listPermissionsNeeded.isEmpty()) {
            requestPermissions(
                    listPermissionsNeeded.toArray(new String[listPermissionsNeeded.size()]),
                    1);
        } else {
            if (getVideoSize())
                createSurfaceView();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions, int[] grantResults) {
        if  (requestCode == 1) {
            if (grantResults.length == 2 && grantResults[0] == PackageManager.PERMISSION_GRANTED &&
                    grantResults[1] == PackageManager.PERMISSION_GRANTED) {
                if (getVideoSize())
                    createSurfaceView();
            }
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUI();
        }
    }

    private void hideSystemUI() {
        // Enables regular immersive mode.
        // For "lean back" mode, remove SYSTEM_UI_FLAG_IMMERSIVE.
        // Or for "sticky immersive," replace it with SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void createSurfaceView() {
        RelativeLayout.LayoutParams layout = new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.WRAP_CONTENT,
                RelativeLayout.LayoutParams.MATCH_PARENT);
        layout.addRule(RelativeLayout.CENTER_HORIZONTAL);

        mVideoView = new VideoView(this, videoWidth, videoHeight);
        mVideoView.setLayoutParams(layout);
        //mVideoView.getHolder().setFormat(PixelFormat.RGB_888);
        mVideoView.getHolder().addCallback(this);

        mMainLayout.addView(mVideoView);
    }

    private boolean getVideoSize() {
        MediaExtractor extractor;
        extractor = new MediaExtractor();

        try {
            extractor.setDataSource(MP4_FILE);

            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat format = extractor.getTrackFormat(i);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime.startsWith("video/")) {
                    videoWidth = format.getInteger(MediaFormat.KEY_WIDTH);
                    videoHeight = format.getInteger(MediaFormat.KEY_HEIGHT);
                    return true;
                }
            }
        } catch (Exception ex) {
            Log.e(TAG, ex.toString());
        }

        return false;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {

    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        mCreated = createStreamingMediaPlayer(MP4_FILE);
        if (mCreated) {
            setSurface(holder.getSurface());
            mPlaying = !mPlaying;
            setPlayingStreamingMediaPlayer(mPlaying);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {

    }

    /** Called when the activity is about to be paused. */
    @Override
    protected void onPause()
    {
        if (mPlaying) {
            mPlaying = !mPlaying;
            setPlayingStreamingMediaPlayer(mPlaying);
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mCreated && !mPlaying) {
            mPlaying = !mPlaying;
            setPlayingStreamingMediaPlayer(mPlaying);
        }
    }

    @Override
    protected void onDestroy()
    {
        shutdown();
        super.onDestroy();
    }

    /** Native methods, implemented in jni folder */
    public static native boolean createStreamingMediaPlayer(String filename);
    public static native void setPlayingStreamingMediaPlayer(boolean isPlaying);
    public static native void shutdown();
    public static native void setSurface(Surface surface);
    public static native void rewindStreamingMediaPlayer();

    /** Load jni .so on initialization */
    static {
        System.loadLibrary("native-codec");
    }
}
