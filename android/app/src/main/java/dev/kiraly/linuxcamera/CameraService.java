package dev.kiraly.linuxcamera;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.hardware.camera2.*;
import android.hardware.camera2.params.*;
import android.os.*;
import android.util.Range;
import android.view.Surface;
import java.util.*;

public final class CameraService extends Service
{
	static volatile String status = "Stopped";
	static final String STATUS_CHANGED = "dev.kiraly.linuxcamera.STATUS_CHANGED";
	static final String CHANNEL = "linux-camera";
	private HandlerThread thread;
	private Handler handler;
	private CameraManager manager;
	private long nativeHandle;
	private List<Catalog.Entry> entries;
	private CameraDevice device;
	private CameraCaptureSession session;
	private Surface surface;
	private PowerManager.WakeLock wake;
	private int active = -1;
	private long generation;
	private volatile boolean shuttingDown;
	private boolean failed;
	private volatile boolean destroyed;
	private boolean retiring;
	private boolean opening;
	private long stopGeneration;

	@Override
	public void onCreate()
	{
		super.onCreate();
		thread = new HandlerThread("linux-camera-control");
		thread.start();
		handler = new Handler(thread.getLooper());
		manager = getSystemService(CameraManager.class);
		wake = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "LinuxCamera:capture");
		getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL, "Linux camera", NotificationManager.IMPORTANCE_LOW));
	}

	private Notification notification()
	{
		PendingIntent open = PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
		PendingIntent stop = PendingIntent.getService(this, 1, new Intent(this, CameraService.class).setAction("stop"), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
		return new Notification.Builder(this, CHANNEL).setSmallIcon(dev.kiraly.linuxcamera.R.drawable.ic_camera).setContentTitle("Linux Camera").setContentText(status).setContentIntent(open).setOngoing(true).addAction(new Notification.Action.Builder(null, "Stop", stop).build()).build();
	}

	private void report(String message)
	{
		status = message;
		sendBroadcast(new Intent(STATUS_CHANGED).setPackage(getPackageName()));
		android.util.Log.i("LinuxCamera", message);
		if (!destroyed)
			getSystemService(NotificationManager.class).notify(1, notification());
	}

	@Override
	public int onStartCommand(Intent intent, int flags, int startId)
	{
		if (intent != null && "stop".equals(intent.getAction()))
		{
			stopSelf();
			return START_NOT_STICKY;
		}
		startForeground(1, notification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA);
		handler.post(() -> {
			if (shuttingDown)
				return;
			if (nativeHandle != 0)
			{
				if (intent != null && intent.hasExtra("packing_mode"))
				{
					String mode = intent.getStringExtra("packing_mode");
					try
					{
						if (!"fast".equals(mode) && !"reference".equals(mode))
							throw new IllegalArgumentException("Unknown packing mode");
						NativeCamera.setPackingMode(nativeHandle, "fast".equals(mode));
						android.util.Log.i("LinuxCamera", "Packing mode: " + mode);
					}
					catch (Exception failure)
					{
						android.util.Log.w("LinuxCamera", "Packing mode unchanged: " + failure.getMessage());
					}
				}
				return;
			}
			try
			{
				report("Starting…");
				nativeHandle = NativeCamera.connect(this);
				entries = Catalog.enumerate(manager);
				if (entries.isEmpty())
					throw new IllegalStateException("No supported raw cameras available");
				for (Catalog.Entry entry : entries)
					entry.index = NativeCamera.registerCamera(nativeHandle, entry.name, entry.identifier, entry.modes);
				NativeCamera.listen(nativeHandle);
				report("Ready: " + entries.size() + " cameras; waiting for a Linux application");
			}
			catch (Exception failure)
			{
				failed = true;
				report("Start failed: " + failure.getMessage());
				shutdown();
				stopSelf();
			}
		});
		return START_NOT_STICKY;
	}

	// Called from the native event worker. All Camera2 lifecycle work stays on
	// one handler; a delayed onOpened/onConfigured cannot revive a stopped epoch.
	public void onRequest(int index, long epoch, int width, int height, int fps, boolean running)
	{
		handler.post(() -> {
			if (shuttingDown || nativeHandle == 0)
				return;
			if (index < 0)
			{
				failed = true;
				report("Broker connection lost");
				shutdown();
				stopSelf();
				return;
			}
			if (!running)
			{
				if (active == index)
					retire(epoch);
				else
					NativeCamera.retire(nativeHandle, index, epoch);
				return;
			}
			try
			{
				if (active != -1)
					throw new IllegalStateException("Previous camera is still retiring");
				active = index;
				generation = epoch;
				wake.acquire();
				Catalog.Entry entry = entries.get(index);
				surface = NativeCamera.surface(nativeHandle, index, epoch, width, height);
				report("Opening " + entry.name + " " + width + "×" + height + " @ " + fps);
				opening = true;
				manager.openCamera(entry.camera, new CameraDevice.StateCallback() {
					@Override
					public void onOpened(CameraDevice opened)
					{
						opening = false;
						device = opened;
						if (shuttingDown || retiring || active != index || generation != epoch)
						{
							opened.close();
							return;
						}
						configure(opened, entry, index, epoch, fps);
					}
					@Override
					public void onDisconnected(CameraDevice disconnected)
					{
						opening = false;
						device = disconnected;
						if (retiring || shuttingDown)
							disconnected.close();
						else
							failure(index, epoch, "Camera disconnected or preempted by Android");
					}
					@Override
					public void onError(CameraDevice broken, int error)
					{
						opening = false;
						device = broken;
						if (retiring || shuttingDown)
							broken.close();
						else
							failure(index, epoch, "Android camera error " + error);
					}
					@Override
					public void onClosed(CameraDevice closed)
					{
						if (device == closed)
							device = null;
						if (retiring || shuttingDown)
							finishRetire();
					}
				}, handler);
			}
			catch (Exception error)
			{
				opening = false;
				failure(index, epoch, error.getMessage());
			}
		});
	}

	private void configure(CameraDevice opened, Catalog.Entry entry, int index, long epoch, int fps)
	{
		try
		{
			OutputConfiguration output = new OutputConfiguration(surface);
			if (entry.physical != null)
				output.setPhysicalCameraId(entry.physical);
			CameraCaptureSession.StateCallback callback = new CameraCaptureSession.StateCallback() {
				@Override
				public void onConfigured(CameraCaptureSession configured)
				{
					if (shuttingDown || retiring || active != index || generation != epoch)
					{
						configured.close();
						return;
					}
					session = configured;
					try
					{
						CaptureRequest.Builder request = opened.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);
						request.addTarget(surface);
						request.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, new Range<>(fps, fps));
						request.set(CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE, CaptureRequest.CONTROL_VIDEO_STABILIZATION_MODE_OFF);
						request.set(CaptureRequest.SCALER_ROTATE_AND_CROP, CaptureRequest.SCALER_ROTATE_AND_CROP_NONE);
						configured.setRepeatingRequest(request.build(), null, handler);
						report("Streaming " + entry.name + " @ " + fps + " fps; native orientation");
					}
					catch (Exception error)
					{
						failure(index, epoch, error.getMessage());
					}
				}
				@Override
				public void onConfigureFailed(CameraCaptureSession failed)
				{
					failure(index, epoch, "Camera rejected this lens/mode combination");
				}
			};
			SessionConfiguration config = new SessionConfiguration(SessionConfiguration.SESSION_REGULAR, Collections.singletonList(output), task -> handler.post(task), callback);
			if (!opened.isSessionConfigurationSupported(config))
				throw new IllegalStateException("Unsupported physical camera stream");
			opened.createCaptureSession(config);
		}
		catch (Exception error)
		{
			failure(index, epoch, error.getMessage());
		}
	}

	private void failure(int index, long epoch, String reason)
	{
		report("Capture error: " + reason);
		if (nativeHandle != 0)
			NativeCamera.fail(nativeHandle, index, epoch);
	}

	private void retire(long epoch)
	{
		retiring = true;
		stopGeneration = epoch;
		if (session != null)
		{
			session.close();
			session = null;
		}
		if (device != null)
			device.close();
		else if (!opening)
			finishRetire();
	}

	private void finishRetire()
	{
		if (surface != null)
		{
			surface.release();
			surface = null;
		}
		if (nativeHandle != 0 && active >= 0)
		{
			android.util.Log.i("LinuxCamera", NativeCamera.statistics(nativeHandle));
			NativeCamera.retire(nativeHandle, active, stopGeneration);
		}
		active = -1;
		retiring = false;
		if (wake.isHeld())
			wake.release();
		if (shuttingDown)
		{
			if (nativeHandle != 0)
			{
				NativeCamera.disconnect(nativeHandle);
				nativeHandle = 0;
			}
			if (!failed)
				report("Stopped");
			thread.quitSafely();
		}
		else
			report("Ready; camera released");
	}

	private void shutdown()
	{
		if (shuttingDown)
			return;
		shuttingDown = true;
		retire(generation);
	}

	@Override
	public void onDestroy()
	{
		destroyed = true;
		if (!shuttingDown)
			handler.post(this::shutdown);
		stopForeground(STOP_FOREGROUND_REMOVE);
		super.onDestroy();
	}

	@Override
	public IBinder onBind(Intent intent)
	{
		return null;
	}
}
