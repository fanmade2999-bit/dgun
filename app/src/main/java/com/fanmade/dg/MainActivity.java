package com.fanmade.dg;

import android.app.Activity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.opengl.GLSurfaceView;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.content.Context;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Thin Android shell. Simulation and rendering live in native C++.
 * Build 0016 keeps the Android layer intentionally thin; native C++ owns
 * persistence, simulation, rendering, and input state.
 */
public final class MainActivity extends Activity {
    private GameSurface surface;
    private FrameLayout root;
    private LinearLayout commandPanel;
    private EditText commandInput;
    private LinearLayout suggestionList;
    private TextView commandResult;
    private final Handler uiHandler = new Handler(Looper.getMainLooper());

    private final Runnable nativeCommandPoller = new Runnable() {
        @Override public void run() {
            try {
                if (NativeBridge.consumeCommandOpenRequest()) showCommandConsole();
                final String result = NativeBridge.consumeCommandResult();
                if (result != null && !result.isEmpty() && commandResult != null)
                    commandResult.setText(result);
            } catch (Throwable ignored) {}
            uiHandler.postDelayed(this, 80L);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        getWindow().setFlags(1024, 1024); // FLAG_FULLSCREEN
        surface = new GameSurface();
        root = new FrameLayout(this);
        root.addView(surface, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        buildCommandConsole();
        setContentView(root);
        uiHandler.post(nativeCommandPoller);
    }

    private GradientDrawable commandBackground() {
        final GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(238, 5, 12, 20));
        bg.setStroke(2, Color.argb(190, 90, 210, 235));
        bg.setCornerRadius(10);
        return bg;
    }

    private void buildCommandConsole() {
        commandPanel = new LinearLayout(this);
        commandPanel.setOrientation(LinearLayout.VERTICAL);
        commandPanel.setPadding(18, 14, 18, 14);
        commandPanel.setBackground(commandBackground());
        commandPanel.setVisibility(android.view.View.GONE);

        final FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                (int)(getResources().getDisplayMetrics().widthPixels * 0.78f),
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
        params.leftMargin = 18;
        params.topMargin = 18;

        final LinearLayout titleRow = new LinearLayout(this);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);

        final TextView title = new TextView(this);
        title.setText("ADMIN COMMAND");
        title.setTextColor(Color.rgb(190, 235, 250));
        title.setTextSize(15);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        titleRow.addView(title, new LinearLayout.LayoutParams(0, 44, 1));

        final Button close = new Button(this);
        close.setText("X");
        close.setTextColor(Color.WHITE);
        close.setTextSize(12);
        close.setAllCaps(false);
        close.setOnClickListener(v -> hideCommandConsole());
        titleRow.addView(close, new LinearLayout.LayoutParams(54, 44));
        commandPanel.addView(titleRow);

        commandInput = new EditText(this);
        commandInput.setSingleLine(true);
        commandInput.setHint("/summon npc mechanic");
        commandInput.setHintTextColor(Color.argb(150, 180, 210, 220));
        commandInput.setTextColor(Color.WHITE);
        commandInput.setTextSize(15);
        commandInput.setInputType(android.text.InputType.TYPE_CLASS_TEXT |
                                  android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        commandInput.setImeOptions(EditorInfo.IME_ACTION_GO);
        commandInput.setPadding(12, 0, 12, 0);
        commandInput.setBackgroundColor(Color.argb(120, 20, 35, 46));
        commandPanel.addView(commandInput, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 46));

        commandResult = new TextView(this);
        commandResult.setText("TYPE A COMMAND");
        commandResult.setTextColor(Color.rgb(150, 215, 235));
        commandResult.setTextSize(12);
        commandResult.setPadding(8, 8, 8, 4);
        commandPanel.addView(commandResult);

        final TextView suggestionTitle = new TextView(this);
        suggestionTitle.setText("SUGGESTIONS");
        suggestionTitle.setTextColor(Color.rgb(120, 165, 180));
        suggestionTitle.setTextSize(11);
        suggestionTitle.setPadding(8, 4, 8, 5);
        commandPanel.addView(suggestionTitle);

        final ScrollView scroll = new ScrollView(this);
        suggestionList = new LinearLayout(this);
        suggestionList.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(suggestionList);
        commandPanel.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 230));

        root.addView(commandPanel, params);

        commandInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence t, int start, int before, int count) {
                refreshCommandSuggestions(t == null ? "" : t.toString());
            }
            @Override public void afterTextChanged(Editable e) {}
        });

        commandInput.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_GO ||
                (event != null && event.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER)) {
                submitCommand();
                return true;
            }
            return false;
        });
    }

    private void showCommandConsole() {
        if (commandPanel == null) return;
        commandPanel.setVisibility(android.view.View.VISIBLE);
        refreshCommandSuggestions(commandInput.getText().toString());
        commandInput.requestFocus();
        final InputMethodManager imm =
                (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.showSoftInput(commandInput, InputMethodManager.SHOW_IMPLICIT);
    }

    private void hideCommandConsole() {
        if (commandPanel == null) return;
        commandPanel.setVisibility(android.view.View.GONE);
        final InputMethodManager imm =
                (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(commandInput.getWindowToken(), 0);
        NativeBridge.cancelCommandOpenRequest();
    }

    private void submitCommand() {
        final String text = commandInput.getText().toString().trim();
        if (text.isEmpty()) return;
        NativeBridge.queueCommand(text);
        commandResult.setText("EXECUTING...");
        commandInput.setText("");
    }

    private void refreshCommandSuggestions(String text) {
        if (suggestionList == null) return;
        suggestionList.removeAllViews();
        final String raw = NativeBridge.getCommandSuggestions(text);
        if (raw == null || raw.isEmpty()) return;
        final String[] suggestions = raw.split("\\n");
        final int limit = Math.min(8, suggestions.length);
        for (int i = 0; i < limit; i++) {
            final String suggestion = suggestions[i];
            final Button b = new Button(this);
            b.setText(suggestion);
            b.setTextColor(Color.rgb(205, 238, 248));
            b.setTextSize(12);
            b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            b.setAllCaps(false);
            b.setPadding(10, 0, 10, 0);
            b.setOnClickListener(v -> {
                commandInput.setText(suggestion);
                commandInput.setSelection(commandInput.length());
            });
            suggestionList.addView(b, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 40));
        }
    }

    @Override protected void onResume() {
        super.onResume();
        if (surface != null) surface.onResume();
    }

    @Override public void onBackPressed() {
        if (commandPanel != null && commandPanel.getVisibility() == android.view.View.VISIBLE) {
            hideCommandConsole();
            return;
        }
        super.onBackPressed();
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
            setRenderer(new MainActivity.Renderer(
                    MainActivity.this.getFilesDir().getAbsolutePath()));
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
        private final String savePath;

        Renderer(String savePath) {
            this.savePath = savePath;
        }

        @Override public void onSurfaceCreated(
                javax.microedition.khronos.opengles.GL10 gl,
                javax.microedition.khronos.egl.EGLConfig config) {
            NativeBridge.init(savePath);
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
        static native boolean consumeCommandOpenRequest();
        static native void cancelCommandOpenRequest();
        static native void queueCommand(String text);
        static native String consumeCommandResult();
        static native String getCommandSuggestions(String text);
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
