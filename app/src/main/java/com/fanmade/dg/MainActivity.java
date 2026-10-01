package com.fanmade.dg;

import android.app.Activity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.opengl.GLSurfaceView;
import android.content.pm.ActivityInfo;

/**
 * Thin Android shell. Simulation and rendering live in native C++.
 * Build 0001 intentionally keeps Java-side logic minimal.
 */
public final class MainActivity extends Activity {
    private GameSurface surface;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        getWindow().setFlags(1024, 1024); // FLAG_FULLSCREEN
        surface = new GameSurface();
        setContentView(surface);
    }

    @Override protected void onResume() {
        super.onResume();
        if (surface != null) surface.onResume();
    }

    @Override protected void onPause() {
        if (surface != null) surface.onPause();
        super.onPause();
    }

    private final class GameSurface extends GLSurfaceView {
        GameSurface() {
            super(MainActivity.this);
            setEGLContextClientVersion(2);
            setPreserveEGLContextOnPause(true);
            setRenderer(new Renderer());
            setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
            setFocusable(true);
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            final int action = e.getActionMasked();
            final int count = e.getPointerCount();
            for (int i = 0; i < count; i++) {
                NativeBridge.touch(i, action, e.getX(i), e.getY(i), getWidth(), getHeight());
            }
            return true;
        }
    }

    private static final class Renderer implements GLSurfaceView.Renderer {
        @Override public void onSurfaceCreated(javax.microedition.khronos.egl.EGLConfig config) {
            NativeBridge.init();
        }
        @Override public void onSurfaceChanged(javax.microedition.khronos.opengles.GL10 gl, int w, int h) {
            NativeBridge.resize(w, h);
        }
        @Override public void onDrawFrame(javax.microedition.khronos.opengles.GL10 gl) {
            NativeBridge.frame();
        }
    }

    private static final class NativeBridge {
        static { System.loadLibrary("dgcore"); }
        static native void init();
        static native void resize(int width, int height);
        static native void frame();
        static native void touch(int pointer, int action, float x, float y, int width, int height);
    }
}
