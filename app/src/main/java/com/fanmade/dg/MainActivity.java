package com.fanmade.dg;

import android.app.Activity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.opengl.GLSurfaceView;
import android.content.pm.ActivityInfo;

/**
 * Thin Android shell. Simulation and rendering live in native C++.
 * Build 0002 adds proper multi-touch pointer routing.
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
        NativeBridge.save();
        if (surface != null) surface.onPause();
        super.onPause();
    }

    private final class GameSurface extends GLSurfaceView {
        GameSurface() {
            super(MainActivity.this);
            setEGLContextClientVersion(2);
            setPreserveEGLContextOnPause(true);
            setRenderer(new MainActivity.Renderer());
            setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
            setFocusable(true);
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            final int action = e.getActionMasked();

            // On MOVE, forward every active pointer so native code can keep
            // independent movement and fire/aim contacts alive.
            if (action == MotionEvent.ACTION_MOVE) {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    NativeBridge.touch(
                        e.getPointerId(i),
                        action,
                        e.getX(i),
                        e.getY(i),
                        getWidth(),
                        getHeight()
                    );
                }
                return true;
            }

            // For down/up/pointer-down/pointer-up/cancel, only the changed
            // pointer needs to be delivered (cancel clears all in native).
            final int index = e.getActionIndex();
            if (action == MotionEvent.ACTION_CANCEL) {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    NativeBridge.touch(
                        e.getPointerId(i),
                        action,
                        e.getX(i),
                        e.getY(i),
                        getWidth(),
                        getHeight()
                    );
                }
            } else {
                NativeBridge.touch(
                    e.getPointerId(index),
                    action,
                    e.getX(index),
                    e.getY(index),
                    getWidth(),
                    getHeight()
                );
            }
            return true;
        }
    }

    private static final class Renderer implements GLSurfaceView.Renderer {
        @Override public void onSurfaceCreated(
                javax.microedition.khronos.opengles.GL10 gl,
                javax.microedition.khronos.egl.EGLConfig config) {
            NativeBridge.init(getFilesDir().getAbsolutePath());
        }

        @Override public void onSurfaceChanged(
                javax.microedition.khronos.opengles.GL10 gl, int w, int h) {
            NativeBridge.resize(w, h);
        }

        @Override public void onDrawFrame(
                javax.microedition.khronos.opengles.GL10 gl) {
            NativeBridge.frame();
        }
    }

    private static final class NativeBridge {
        static { System.loadLibrary("dgcore"); }

        static native void init(String savePath);
        static native void save();
        static native void resize(int width, int height);
        static native void frame();
        static native void touch(
                int pointerId,
                int action,
                float x,
                float y,
                int width,
                int height
        );
    }
}
