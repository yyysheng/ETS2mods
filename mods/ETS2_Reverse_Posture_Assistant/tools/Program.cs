using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

const int width = 1280;
const int height = 720;

string projectRoot = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
string materialDir = Path.Combine(projectRoot, "mod", "material", "ui", "reverse_path");
Directory.CreateDirectory(materialDir);

using var reverse = new Bitmap(width, height, PixelFormat.Format32bppArgb);
using (Graphics g = Graphics.FromImage(reverse))
{
    g.SmoothingMode = SmoothingMode.AntiAlias;
    g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
    g.Clear(Color.FromArgb(255, 12, 16, 20));

    using var gridPen = new Pen(Color.FromArgb(32, 255, 255, 255), 1);
    for (int x = 0; x <= width; x += 80) g.DrawLine(gridPen, x, 0, x, height);
    for (int y = 0; y <= height; y += 80) g.DrawLine(gridPen, 0, y, width, y);

    using var headerBrush = new SolidBrush(Color.FromArgb(245, 24, 30, 36));
    g.FillRectangle(headerBrush, 0, 0, width, 82);
    using var accentBrush = new SolidBrush(Color.FromArgb(255, 46, 204, 113));
    g.FillRectangle(accentBrush, 0, 80, width, 3);

    using var titleFont = new Font("Microsoft YaHei UI", 27, FontStyle.Bold, GraphicsUnit.Pixel);
    using var smallFont = new Font("Microsoft YaHei UI", 18, FontStyle.Regular, GraphicsUnit.Pixel);
    using var rFont = new Font("Segoe UI", 42, FontStyle.Bold, GraphicsUnit.Pixel);
    using var white = new SolidBrush(Color.FromArgb(245, 245, 247, 249));
    using var muted = new SolidBrush(Color.FromArgb(210, 176, 185, 194));
    using var rBrush = new SolidBrush(Color.FromArgb(255, 255, 194, 61));
    g.DrawString("R", rFont, rBrush, 28, 14);
    g.DrawString("倒车姿态助手", titleFont, white, 92, 20);
    g.DrawString("30 m 环境轮廓", smallFont, muted, 1060, 29);

    // Preview artwork only. The injected runtime replaces this texture with live paths.
    DrawPath(g, new[] { new PointF(496, 435), new PointF(470, 520), new PointF(445, 610), new PointF(420, 704) }, Color.FromArgb(255, 55, 220, 125), 10);
    DrawPath(g, new[] { new PointF(784, 435), new PointF(810, 520), new PointF(835, 610), new PointF(860, 704) }, Color.FromArgb(255, 55, 220, 125), 10);
    DrawPath(g, new[] { new PointF(555, 435), new PointF(542, 520), new PointF(530, 610), new PointF(518, 704) }, Color.FromArgb(230, 255, 204, 64), 5);
    DrawPath(g, new[] { new PointF(725, 435), new PointF(738, 520), new PointF(750, 610), new PointF(762, 704) }, Color.FromArgb(230, 255, 204, 64), 5);

    using var centerPen = new Pen(Color.FromArgb(190, 235, 239, 242), 3) { DashStyle = DashStyle.Dash };
    g.DrawLine(centerPen, 640, 435, 640, 716);

    DrawDistanceBar(g, 480, 500, 800, Color.FromArgb(255, 231, 76, 60), "0.5 m", smallFont);
    DrawDistanceBar(g, 458, 575, 822, Color.FromArgb(255, 255, 190, 46), "1.5 m", smallFont);
    DrawDistanceBar(g, 438, 660, 842, Color.FromArgb(255, 46, 204, 113), "3.0 m", smallFont);

    DrawTruckAndTrailer(g);

    using var noteBrush = new SolidBrush(Color.FromArgb(220, 188, 197, 205));
    g.DrawString("请同时观察后视镜", smallFont, noteBrush, 28, 674);
}

string previewPath = Path.Combine(projectRoot, "reverse_screen_preview.png");
reverse.Save(previewPath, ImageFormat.Png);
using var transparentOverlay = new Bitmap(1021, 577, PixelFormat.Format32bppArgb);
WriteDds(Path.Combine(materialDir, "R.dds"), transparentOverlay);
WriteTobj(Path.Combine(materialDir, "R.tobj"), "/material/ui/reverse_path/R.dds");

using var clear = new Bitmap(4, 4, PixelFormat.Format32bppArgb);
WriteDds(Path.Combine(materialDir, "clear.dds"), clear);
WriteTobj(Path.Combine(materialDir, "clear.tobj"), "/material/ui/reverse_path/clear.dds");

using var icon = new Bitmap(276, 162, PixelFormat.Format24bppRgb);
using (Graphics g = Graphics.FromImage(icon))
{
    g.SmoothingMode = SmoothingMode.HighQuality;
    g.DrawImage(reverse, new Rectangle(0, 0, icon.Width, icon.Height));
}
icon.Save(Path.Combine(projectRoot, "mod", "mod_icon.jpg"), ImageFormat.Jpeg);

static void DrawPath(Graphics g, PointF[] points, Color color, float width)
{
    using var path = new GraphicsPath();
    path.AddCurve(points, 0.35f);
    using var glow = new Pen(Color.FromArgb(60, color), width + 12) { StartCap = LineCap.Round, EndCap = LineCap.Round };
    using var pen = new Pen(color, width) { StartCap = LineCap.Round, EndCap = LineCap.Round };
    g.DrawPath(glow, path);
    g.DrawPath(pen, path);
}

static void DrawDistanceBar(Graphics g, float left, float y, float right, Color color, string label, Font font)
{
    using var pen = new Pen(color, 7) { StartCap = LineCap.Round, EndCap = LineCap.Round };
    g.DrawLine(pen, left, y, right, y);
    using var brush = new SolidBrush(color);
    g.DrawString(label, font, brush, right + 16, y - 14);
}

static void DrawTruckAndTrailer(Graphics g)
{
    using var shadow = new SolidBrush(Color.FromArgb(90, 0, 0, 0));
    using var trailerBrush = new SolidBrush(Color.FromArgb(255, 75, 88, 101));
    using var truckBrush = new SolidBrush(Color.FromArgb(255, 224, 231, 236));
    using var edgePen = new Pen(Color.FromArgb(255, 16, 20, 24), 5);
    using var windowBrush = new SolidBrush(Color.FromArgb(255, 62, 115, 139));

    RectangleF trailer = new(530, 215, 220, 230);
    RectangleF cab = new(548, 96, 184, 112);
    g.FillRectangle(shadow, trailer.X + 9, trailer.Y + 9, trailer.Width, trailer.Height);
    g.FillRectangle(trailerBrush, trailer);
    g.DrawRectangle(edgePen, trailer.X, trailer.Y, trailer.Width, trailer.Height);
    g.FillRectangle(shadow, cab.X + 9, cab.Y + 9, cab.Width, cab.Height);
    g.FillRectangle(truckBrush, cab);
    g.DrawRectangle(edgePen, cab.X, cab.Y, cab.Width, cab.Height);
    g.FillRectangle(windowBrush, 575, 112, 130, 34);
    g.DrawLine(edgePen, 640, 148, 640, 205);

    using var wheelBrush = new SolidBrush(Color.FromArgb(255, 28, 32, 36));
    foreach (float y in new[] { 170f, 250f, 390f })
    {
        g.FillRectangle(wheelBrush, 510, y, 20, 38);
        g.FillRectangle(wheelBrush, 750, y, 20, 38);
    }
}

static void WriteDds(string path, Bitmap bitmap)
{
    using var output = new BinaryWriter(File.Create(path));
    output.Write(new byte[] { (byte)'D', (byte)'D', (byte)'S', (byte)' ' });
    output.Write(124u);
    output.Write(0x0000100Fu); // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    output.Write((uint)bitmap.Height);
    output.Write((uint)bitmap.Width);
    output.Write((uint)(bitmap.Width * 4));
    output.Write(0u);
    output.Write(0u);
    for (int i = 0; i < 11; i++) output.Write(0u);
    output.Write(32u);
    output.Write(4u); // FOURCC
    output.Write(new byte[] { (byte)'D', (byte)'X', (byte)'1', (byte)'0' });
    output.Write(0u); output.Write(0u); output.Write(0u); output.Write(0u); output.Write(0u);
    output.Write(0x1000u); // DDSCAPS_TEXTURE
    output.Write(0u); output.Write(0u); output.Write(0u); output.Write(0u);
    output.Write(91u); // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
    output.Write(3u);  // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    output.Write(0u);
    output.Write(1u);
    output.Write(0u);

    var rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
    BitmapData data = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    try
    {
        byte[] row = new byte[bitmap.Width * 4];
        for (int y = 0; y < bitmap.Height; y++)
        {
            Marshal.Copy(data.Scan0 + y * data.Stride, row, 0, row.Length);
            output.Write(row);
        }
    }
    finally
    {
        bitmap.UnlockBits(data);
    }
}

static void WriteTobj(string path, string texturePath)
{
    byte[] header =
    {
        0x01, 0x0A, 0xB1, 0x70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 1, 1, 2, 0, 2, 2,
        0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    byte[] encodedPath = System.Text.Encoding.ASCII.GetBytes(texturePath);
    BitConverter.GetBytes(encodedPath.Length).CopyTo(header, 40);
    using var output = File.Create(path);
    output.Write(header);
    output.Write(encodedPath);
}
