package com.testapp.h264nativedecoder;

import android.content.Context;
import android.view.SurfaceView;

public class VideoView extends SurfaceView {
    private int mVideoWidth, mVideoHeight;

    public VideoView(Context context, int videoWidth, int videoHeight) {
        super(context);
        mVideoWidth = videoWidth;
        mVideoHeight = videoHeight;
    }

    @Override
    protected void onMeasure (int widthMeasureSpec, int heightMeasureSpec) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);

        final int height = getMeasuredHeight();
        final int width = mVideoWidth*height/mVideoHeight; //parent width

        setMeasuredDimension(width, height);
    }
}
