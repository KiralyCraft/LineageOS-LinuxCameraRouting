package dev.kiraly.linuxcamera;

import android.view.Surface;
import java.io.IOException;

final class NativeCamera
{
	static
	{
		System.loadLibrary("linux_camera");
	}
	static native long connect(CameraService service) throws IOException;
	static native int registerCamera(long handle, String name, String identifier, int[] modes) throws IOException;
	static native void listen(long handle) throws IOException;
	static native Surface surface(long handle, int index, long generation, int width, int height) throws IOException;
	static native void retire(long handle, int index, long generation);
	static native void fail(long handle, int index, long generation);
	static native String statistics(long handle);
	static native void disconnect(long handle);
}
