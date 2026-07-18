using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;
using TsMap;
using TsMap.TsItem;

internal static class Program
{
    private const uint Magic = 0x45545345; // ETSE
    private const int HeaderSize = 64;
    private const int SegmentSize = 28;
    private const int MaxSegments = 12000;
    private const int MappingSize = HeaderSize + MaxSegments * SegmentSize;
    private const float CellSize = 128.0f;
    private const float QueryRadius = 30.0f;
    private const uint IndexVersion = 5;
    private static readonly string LogPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ETS2ReverseEnvironment.log");

    private struct MapSegment
    {
        public float X1, Z1, X2, Z2, Width;
        public uint Color, Kind;
        public long CellKey;
    }

    private struct CellRange
    {
        public int Start, Count;
    }

    private static readonly List<MapSegment> Segments = new List<MapSegment>();
    private static readonly Dictionary<long, CellRange> Grid = new Dictionary<long, CellRange>();

    private static int Main(string[] args)
    {
        using (var mutex = new Mutex(true, "Local\\ETS2ReverseEnvironmentService", out bool ownsMutex))
        {
            if (!ownsMutex) return 0;

            string gameDir = args.Length > 0
                ? Path.GetFullPath(args[0])
                : Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", ".."));
            Process gameProcess = null;
            if (args.Length > 1 && int.TryParse(args[1], out int gameProcessId))
            {
                try { gameProcess = Process.GetProcessById(gameProcessId); }
                catch (ArgumentException) { return 0; }
            }
            Environment.CurrentDirectory = AppDomain.CurrentDomain.BaseDirectory;

            try
            {
                string indexPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ETS2TerrainIndex.bin");
                bool loaded = false;
                if (File.Exists(indexPath))
                {
                    try
                    {
                        LoadIndex(indexPath);
                        loaded = true;
                    }
                    catch (InvalidDataException)
                    {
                        Segments.Clear();
                        Grid.Clear();
                    }
                }
                if (!loaded)
                {
                    var mapper = new TsMapper(gameDir, new List<Mod>());
                    mapper.Parse();
                    MaterializeGeometry(mapper);
                    BuildSpatialIndex(mapper);
                    SaveIndex(indexPath);
                }
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                File.AppendAllText(LogPath, DateTime.Now.ToString("O") + " index=" + IndexVersion +
                    " segments=" + Segments.Count + " game_pid=" + (gameProcess == null ? 0 : gameProcess.Id) + Environment.NewLine);
            }
            catch (Exception ex)
            {
                File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ETS2ReverseEnvironment.error.log"), ex.ToString());
                return 1;
            }

            using (var output = MemoryMappedFile.CreateOrOpen("Local\\ETS2ReverseEnvironment", MappingSize, MemoryMappedFileAccess.ReadWrite))
            using (var writer = output.CreateViewAccessor(0, MappingSize, MemoryMappedFileAccess.ReadWrite))
            {
                writer.Write(0, Magic);
                writer.Write(4, 1u);
                Run(writer, gameProcess);
            }
        }
        return 0;
    }

    private static void BuildSpatialIndex(TsMapper mapper)
    {
        foreach (var road in mapper.Roads)
        {
            PointF[] points = road.GetPoints();
            if (points == null || points.Length < 2 || road.RoadLook == null) continue;
            float width = Math.Max(2.5f, Math.Min(road.RoadLook.GetWidth(), 20.0f));
            for (int i = 1; i < points.Length; ++i)
                AddSegment(points[i - 1], points[i], width, 0xFF34434A, 1);
        }

        foreach (var prefab in mapper.Prefabs)
        {
            foreach (TsPrefabLook look in prefab.GetLooks())
            {
                PointF[] points = look.GetPoints();
                if (points == null || points.Length < 2) continue;
                var roadLook = look as TsPrefabRoadLook;
                float width = roadLook == null ? 0.45f : Math.Max(2.0f, Math.Min(roadLook.GetWidth(), 24.0f));
                uint color = roadLook == null ? 0xFF2B383D : 0xFF3A4B52;
                uint kind = roadLook == null ? 3u : 2u;
                for (int i = 1; i < points.Length; ++i)
                    AddSegment(points[i - 1], points[i], width, color, kind);
                if (roadLook == null && points.Length > 2)
                    AddSegment(points[points.Length - 1], points[0], width, color, kind);
            }
        }

        foreach (TsStaticModel model in mapper.Models)
        {
            if (!TryGetModelSize(model.ModelPath, out float length, out float width)) continue;
            AddModelOutline(model.X, model.Z, model.Rotation, length, width);
        }

        Segments.Sort((a, b) => a.CellKey.CompareTo(b.CellKey));
        BuildCellRanges();
    }

    private static bool TryGetModelSize(string modelPath, out float length, out float width)
    {
        string path = modelPath.ToLowerInvariant();
        length = 0.0f;
        width = 0.0f;
        if (path.Contains("parked_trailers")) { length = path.Contains("8m") ? 8.5f : 13.7f; width = 2.7f; }
        else if (path.Contains("parked_trucks")) { length = path.Contains("trailer") ? 18.0f : 7.2f; width = 2.7f; }
        else if (path.Contains("parked_cars_groups")) { length = 18.0f; width = 10.0f; }
        else if (path.Contains("parked_cars")) { length = path.Contains("transit") || path.Contains("ducato") || path.Contains("van") ? 6.2f : 4.9f; width = 2.1f; }
        else if (path.Contains("excavator")) { length = 8.5f; width = 3.2f; }
        else if (path.Contains("bulldozer")) { length = 6.5f; width = 3.2f; }
        else if (path.Contains("wheel_loader")) { length = 8.0f; width = 3.1f; }
        else if (path.Contains("backhoe")) { length = 7.5f; width = 2.8f; }
        else if (path.Contains("forklift")) { length = 3.8f; width = 1.8f; }
        else if (path.Contains("harvester")) { length = 9.0f; width = 3.8f; }
        else if (path.Contains("tractor")) { length = 5.5f; width = 2.6f; }
        else if (path.Contains("/vehicle/machine/") || path.Contains("/asset/vehicle/")) { length = 8.0f; width = 3.0f; }
        else if (path.Contains("/model2/vehicle/")) { length = 6.0f; width = 2.5f; }
        return length > 0.0f;
    }

    private static void AddModelOutline(float centerX, float centerZ, float rotation, float length, float width, uint kind = 10)
    {
        float forwardX = (float)Math.Sin(rotation);
        float forwardZ = (float)Math.Cos(rotation);
        float sideX = forwardZ;
        float sideZ = -forwardX;
        float halfLength = length * 0.5f;
        float halfWidth = width * 0.5f;
        var corners = new[]
        {
            new PointF(centerX + forwardX * halfLength + sideX * halfWidth, centerZ + forwardZ * halfLength + sideZ * halfWidth),
            new PointF(centerX + forwardX * halfLength - sideX * halfWidth, centerZ + forwardZ * halfLength - sideZ * halfWidth),
            new PointF(centerX - forwardX * halfLength - sideX * halfWidth, centerZ - forwardZ * halfLength - sideZ * halfWidth),
            new PointF(centerX - forwardX * halfLength + sideX * halfWidth, centerZ - forwardZ * halfLength + sideZ * halfWidth)
        };
        for (int i = 0; i < corners.Length; ++i)
            AddSegment(corners[i], corners[(i + 1) % corners.Length], 0.35f, 0xFFE06A4F, kind);
    }

    private static void MaterializeGeometry(TsMapper mapper)
    {
        const int size = 1024;
        float spanX = Math.Max(1.0f, mapper.maxX - mapper.minX);
        float spanZ = Math.Max(1.0f, mapper.maxZ - mapper.minZ);
        float scale = Math.Min(size / spanX, size / spanZ);
        using (var bitmap = new Bitmap(size, size))
        using (Graphics graphics = Graphics.FromImage(bitmap))
        {
            var palette = new MapPalette
            {
                Background = Brushes.Black,
                Road = Brushes.Gray,
                PrefabRoad = Brushes.Gray,
                PrefabLight = Brushes.DarkGray,
                PrefabDark = Brushes.DimGray,
                PrefabGreen = Brushes.DarkGreen,
                CityName = Brushes.Transparent,
                FerryLines = Brushes.Transparent,
                Error = Brushes.Red
            };
            new TsMapRenderer(mapper).Render(
                graphics,
                new Rectangle(0, 0, size, size),
                scale,
                new PointF(mapper.minX, mapper.minZ),
                palette,
                RenderFlags.Roads | RenderFlags.Prefabs | RenderFlags.SecretRoads);
        }
    }

    private static void AddSegment(PointF a, PointF b, float width, uint color, uint kind)
    {
        float dx = b.X - a.X, dz = b.Y - a.Y;
        if (dx * dx + dz * dz > 40000.0f) return;
        long cellKey = Key(Cell((a.X + b.X) * 0.5f), Cell((a.Y + b.Y) * 0.5f));
        Segments.Add(new MapSegment { X1 = a.X, Z1 = a.Y, X2 = b.X, Z2 = b.Y, Width = width, Color = color, Kind = kind, CellKey = cellKey });
    }

    private static void BuildCellRanges()
    {
        Grid.Clear();
        int start = 0;
        while (start < Segments.Count)
        {
            long key = Segments[start].CellKey;
            int end = start + 1;
            while (end < Segments.Count && Segments[end].CellKey == key) ++end;
            Grid[key] = new CellRange { Start = start, Count = end - start };
            start = end;
        }
    }

    private static void SaveIndex(string path)
    {
        using (var writer = new BinaryWriter(File.Create(path)))
        {
            writer.Write(0x45544958u); // ETIX
            writer.Write(IndexVersion);
            writer.Write(Segments.Count);
            foreach (MapSegment segment in Segments)
            {
                writer.Write(segment.X1); writer.Write(segment.Z1);
                writer.Write(segment.X2); writer.Write(segment.Z2);
                writer.Write(segment.Width); writer.Write(segment.Color);
                writer.Write(segment.Kind); writer.Write(segment.CellKey);
            }
        }
    }

    private static void LoadIndex(string path)
    {
        using (var reader = new BinaryReader(File.OpenRead(path)))
        {
            if (reader.ReadUInt32() != 0x45544958u || reader.ReadUInt32() != IndexVersion)
                throw new InvalidDataException("Unsupported terrain index.");
            int count = reader.ReadInt32();
            if (count <= 0 || count > 10000000) throw new InvalidDataException("Invalid terrain segment count.");
            Segments.Capacity = count;
            for (int i = 0; i < count; ++i)
            {
                Segments.Add(new MapSegment
                {
                    X1 = reader.ReadSingle(), Z1 = reader.ReadSingle(),
                    X2 = reader.ReadSingle(), Z2 = reader.ReadSingle(),
                    Width = reader.ReadSingle(), Color = reader.ReadUInt32(),
                    Kind = reader.ReadUInt32(), CellKey = reader.ReadInt64()
                });
            }
        }
        BuildCellRanges();
    }

    private static void Run(MemoryMappedViewAccessor writer, Process gameProcess)
    {
        uint sequence = 0;
        int missingTelemetryTicks = 0;
        while (gameProcess != null || missingTelemetryTicks < 600)
        {
            if (GameExited(gameProcess)) return;
            try
            {
                using (var telemetry = MemoryMappedFile.OpenExisting("Local\\SCSTelemetry", MemoryMappedFileRights.Read))
                using (var reader = telemetry.CreateViewAccessor(0, 32768, MemoryMappedFileAccess.Read))
                {
                    missingTelemetryTicks = 0;
                    while (true)
                    {
                        if (GameExited(gameProcess)) return;
                        double truckX = reader.ReadDouble(2200);
                        double truckZ = reader.ReadDouble(2216);
                        double heading = reader.ReadDouble(2224) * Math.PI * 2.0;
                        int count = WriteNearby(writer, truckX, truckZ, heading, ++sequence);
                        if (sequence % 150 == 0)
                            File.AppendAllText(LogPath, DateTime.Now.ToString("O") + " seq=" + sequence +
                                " position=" + truckX.ToString("F1") + "," + truckZ.ToString("F1") +
                                " nearby=" + count + Environment.NewLine);
                        Thread.Sleep(200);
                    }
                }
            }
            catch (FileNotFoundException)
            {
                ++missingTelemetryTicks;
                Thread.Sleep(200);
            }
            catch (Exception ex)
            {
                File.AppendAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ETS2ReverseEnvironment.error.log"), ex + Environment.NewLine);
                Thread.Sleep(1000);
            }
        }
    }

    private static bool GameExited(Process gameProcess)
    {
        if (gameProcess == null) return false;
        try { return gameProcess.HasExited; }
        catch (InvalidOperationException) { return true; }
    }

    private static int WriteNearby(MemoryMappedViewAccessor writer, double truckX, double truckZ, double heading, uint sequence)
    {
        double cos = Math.Cos(heading), sin = Math.Sin(heading);
        int count = 0;
        int staticModelSegments = 0;
        int minX = Cell((float)truckX - QueryRadius) - 2, maxX = Cell((float)truckX + QueryRadius) + 2;
        int minZ = Cell((float)truckZ - QueryRadius) - 2, maxZ = Cell((float)truckZ + QueryRadius) + 2;
        for (int z = minZ; z <= maxZ && count < MaxSegments; ++z)
            for (int x = minX; x <= maxX && count < MaxSegments; ++x)
                if (Grid.TryGetValue(Key(x, z), out CellRange range))
                    for (int index = range.Start; index < range.Start + range.Count && count < MaxSegments; ++index)
                    {
                        MapSegment segment = Segments[index];
                        double dx1 = segment.X1 - truckX, dz1 = segment.Z1 - truckZ;
                        double dx2 = segment.X2 - truckX, dz2 = segment.Z2 - truckZ;
                        if (Math.Min(dx1 * dx1 + dz1 * dz1, dx2 * dx2 + dz2 * dz2) > QueryRadius * QueryRadius &&
                            (Math.Abs(dx1) > QueryRadius || Math.Abs(dz1) > QueryRadius || Math.Abs(dx2) > QueryRadius || Math.Abs(dz2) > QueryRadius))
                            continue;

                        float localX1 = (float)(cos * dx1 - sin * dz1);
                        float localY1 = (float)(sin * dx1 + cos * dz1);
                        float localX2 = (float)(cos * dx2 - sin * dz2);
                        float localY2 = (float)(sin * dx2 + cos * dz2);
                        long offset = HeaderSize + count * SegmentSize;
                        writer.Write(offset + 0, localX1);
                        writer.Write(offset + 4, localY1);
                        writer.Write(offset + 8, localX2);
                        writer.Write(offset + 12, localY2);
                        writer.Write(offset + 16, segment.Width);
                        writer.Write(offset + 20, segment.Color);
                        writer.Write(offset + 24, segment.Kind);
                        if (segment.Kind >= 10) ++staticModelSegments;
                        ++count;
                    }

        writer.Write(12, count);
        writer.Write(16, truckX);
        writer.Write(24, truckZ);
        writer.Write(32, heading);
        writer.Write(44, staticModelSegments / 4);
        Thread.MemoryBarrier();
        writer.Write(8, sequence);
        return count;
    }

    private static int Cell(float value) => (int)Math.Floor(value / CellSize);
    private static long Key(int x, int z) => ((long)x << 32) ^ (uint)z;
}
