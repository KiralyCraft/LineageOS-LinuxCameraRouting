package dev.kiraly.linuxcamera;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Bundle;
import android.widget.*;

public final class MainActivity extends Activity
{
	private TextView status;
	@Override
	public void onCreate(Bundle state)
	{
		super.onCreate(state);
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setPadding(36, 80, 36, 36);
		layout.setBackgroundColor(Color.rgb(16, 24, 32));
		TextView title = new TextView(this);
		title.setText("Linux Camera");
		title.setTextSize(30);
		layout.addView(title);
		TextView description = new TextView(this);
		description.setText("Use your phone's cameras from Linux. Enable the helper, then choose a camera in your Linux application. Capture stops when the application releases it.");
		description.setTextSize(17);
		description.setPadding(0, 24, 0, 24);
		layout.addView(description);
		status = new TextView(this);
		status.setText(CameraService.status);
		layout.addView(status);
		Button start = new Button(this);
		start.setText("Enable Linux cameras");
		start.setOnClickListener(view -> {
			if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED)
				requestPermissions(new String[] {Manifest.permission.CAMERA, Manifest.permission.POST_NOTIFICATIONS}, 1);
			else
				enable();
		});
		layout.addView(start);
		Button refresh = new Button(this);
		refresh.setText("Refresh status");
		refresh.setOnClickListener(view -> status.setText(CameraService.status));
		layout.addView(refresh);
		Button stop = new Button(this);
		stop.setText("Disable Linux cameras");
		stop.setOnClickListener(view -> { stopService(new Intent(this, CameraService.class)); status.setText("Stopping"); });
		layout.addView(stop);
		setContentView(layout);
	}
	private void enable()
	{
		startForegroundService(new Intent(this, CameraService.class));
		status.setText("Starting…");
	}
	@Override
	public void onRequestPermissionsResult(int request, String[] permissions, int[] results)
	{
		super.onRequestPermissionsResult(request, permissions, results);
		if (request == 1 && checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED)
			enable();
	}
	@Override
	public void onResume()
	{
		super.onResume();
		if (status != null)
			status.setText(CameraService.status);
	}
}
