package dev.kiraly.linuxcamera;

import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.util.Range;
import android.util.Size;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

final class Catalog
{
	static final class Entry
	{
		String camera;
		String physical;
		String name;
		String identifier;
		CameraCharacteristics characteristics;
		int[] modes;
		int index;
	}

	static List<Entry> enumerate(CameraManager manager) throws Exception
	{
		List<Entry> entries = new ArrayList<>();
		for (String id : manager.getCameraIdList())
		{
			CameraCharacteristics info = manager.getCameraCharacteristics(id);
			Integer facing = info.get(CameraCharacteristics.LENS_FACING);
			if (facing == null)
				continue;
			List<String> physical = new ArrayList<>(info.getPhysicalCameraIds());
			physical.sort(String::compareTo);
			if (facing == CameraCharacteristics.LENS_FACING_BACK && !physical.isEmpty())
			{
				int previousCount = entries.size();
				for (String lens : physical)
				{
					try
					{
						CameraCharacteristics lensInfo = manager.getCameraCharacteristics(lens);
						add(entries, id, lens, lensInfo, "Rear", "rear-" + lens);
					}
					catch (Exception unavailable)
					{
						android.util.Log.w("LinuxCamera", "Physical camera " + lens + " unavailable: " + unavailable.getMessage());
					}
				}
				if (entries.size() == previousCount)
					add(entries, id, null, info, "Rear", "rear-" + id);
			}
			else
			{
				String name = "External";
				if (facing == CameraCharacteristics.LENS_FACING_FRONT)
					name = "Front";
				if (facing == CameraCharacteristics.LENS_FACING_BACK)
					name = "Rear";
				add(entries, id, null, info, name, name.toLowerCase(java.util.Locale.ROOT) + "-" + id);
			}
		}
		return entries;
	}

	private static void add(List<Entry> entries, String camera, String physical, CameraCharacteristics info, String name, String identifier)
	{
		// Physical lenses can also appear in the public camera list. Publish each
		// stable endpoint once instead of overwriting its broker alias.
		String endpoint = identifier.replaceAll("[^a-z0-9-]", "-");
		for (Entry existing : entries)
			if (existing.identifier.equals(endpoint))
				return;
		// Unknown timestamp domains are not silently presented as monotonic.
		Integer clock = info.get(CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE);
		if (clock == null || clock != CameraCharacteristics.SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME)
			return;
		StreamConfigurationMap map = info.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
		Range<Integer>[] ranges = info.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
		if (map == null || ranges == null)
			return;
		Size[] sizes = map.getOutputSizes(ImageFormat.YUV_420_888);
		if (sizes == null)
			return;
		List<int[]> modes = new ArrayList<>();
		int[][] wanted = {{1920, 1080}, {1280, 720}, {640, 480}, {3840, 2160}};
		for (int[] size : wanted)
		{
			Size selected = new Size(size[0], size[1]);
			if (!java.util.Arrays.asList(sizes).contains(selected))
				continue;
			long duration = map.getOutputMinFrameDuration(ImageFormat.YUV_420_888, selected);
			for (int fps : new int[] {30, 60, 24, 15})
			{
				boolean fixed = false;
				for (Range<Integer> range : ranges)
					if (range.getLower() == fps && range.getUpper() == fps)
						fixed = true;
				if (fixed && duration > 0 && duration <= 1000000000L / fps + 1000)
					modes.add(new int[] {size[0], size[1], fps});
			}
		}
		if (modes.isEmpty() || entries.size() >= 8)
			return;
		Entry entry = new Entry();
		entry.camera = camera;
		entry.physical = physical;
		entry.characteristics = info;
		entry.identifier = endpoint;
		entry.name = "Linux Camera " + name;
		float[] focal = info.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
		if (physical != null && focal != null && focal.length > 0)
			entry.name += String.format(java.util.Locale.ROOT, " %.2fmm", focal[0]);
		entry.modes = new int[modes.size() * 3];
		for (int i = 0; i < modes.size(); i++)
			System.arraycopy(modes.get(i), 0, entry.modes, i * 3, 3);
		entries.add(entry);
	}
}
