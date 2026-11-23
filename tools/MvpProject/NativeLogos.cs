using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using MVPWorkshop.Core.Formats;

static class NativeLogos
{
    public static byte[] Raster(BitmapSource source,int width,int height,bool contain)
    {
        var visual=new DrawingVisual(); RenderOptions.SetBitmapScalingMode(visual,BitmapScalingMode.HighQuality);
        using(var dc=visual.RenderOpen()) {
            double scale=contain?Math.Min((double)width/source.PixelWidth,(double)height/source.PixelHeight):1;
            var rect=contain?new Rect((width-source.PixelWidth*scale)/2,(height-source.PixelHeight*scale)/2,source.PixelWidth*scale,source.PixelHeight*scale):new Rect(0,0,width,height);
            dc.DrawImage(source,rect);
        }
        var target=new RenderTargetBitmap(width,height,96,96,PixelFormats.Pbgra32);target.Render(visual);
        var straight=new FormatConvertedBitmap(target,PixelFormats.Bgra32,null,0);
        var result=new byte[checked(width*height*4)];straight.CopyPixels(result,width*4,0);return result;
    }
    public static byte[] Dds(BitmapSource source,int width,int height,bool mipmaps)
    {
        var basePixels=Raster(source,width,height,true);
        var canvas=BitmapSource.Create(width,height,96,96,PixelFormats.Bgra32,null,basePixels,width*4);
        var levels=new List<byte[]>();
        for(int w=width,h=height;;w=Math.Max(1,w/2),h=Math.Max(1,h/2)) {
            levels.Add(XboxTexture.EncodeDxt(w==width&&h==height?basePixels:Raster(canvas,w,h,false),w,h,3));
            if(!mipmaps || (w==1&&h==1))break;
        }
        using var ms=new MemoryStream();using var bw=new BinaryWriter(ms,Encoding.ASCII,true);
        bw.Write("DDS "u8);bw.Write(124);bw.Write(0xA1007);bw.Write(height);bw.Write(width);bw.Write(levels[0].Length);bw.Write(0);bw.Write(levels.Count);
        for(int i=0;i<11;i++)bw.Write(0);
        bw.Write(32);bw.Write(4);bw.Write("DXT3"u8);for(int i=0;i<5;i++)bw.Write(0);
        bw.Write(0x401008);for(int i=0;i<4;i++)bw.Write(0);
        foreach(var level in levels)bw.Write(level);return ms.ToArray();
    }
    public static Dictionary<string,byte[]> Build(string source,string template,int mainSize,int wideWidth,int wideHeight,bool mipmaps,bool allSlots=false)
    {
        static bool Power(int n)=>n>=4&&n<=512&&(n&(n-1))==0;
        if(!Power(mainSize)||!Power(wideWidth)||!Power(wideHeight))throw new InvalidDataException("Native logo dimensions must be powers of two, 4–512");
        using var stream=File.OpenRead(source);
        var png=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad).Frames[0];
        var meta=File.ReadAllBytes(Path.Combine(template,"xsh.meta"));
        var original=XshFolder.Build(meta,name=>File.ReadAllBytes(Path.Combine(template,name)),out var error)??throw new InvalidDataException(error);
        var native=SshFile.Read(original);
        var a=native.Images.Single(x=>x.Tag.StartsWith('a'));
        if(a.TypeMasked!=0x7B)throw new InvalidDataException("Expected native PAL8 a slot");
        // Keep indexed dimensions, flags and palette format. Only indices/palette change.
        var changed=SshWriter.ReplaceImage(native,a,Raster(png,a.Header.Width,a.Header.Height,true),a.Header.Width,a.Header.Height,PaletteMode.Rebuild).File;
        var parts=XshFolder.Split(changed)??throw new InvalidDataException("Cannot split native logo after palette rebuild");
        var j=native.Images.Single(x=>x.Tag.StartsWith('j'));var i=native.Images.Single(x=>x.Tag.StartsWith('i'));
        if(i.TypeMasked!=0x61||j.TypeMasked!=0x61)throw new InvalidDataException("Expected native DXT3 i/j slots");
        string Slot(SshImage image)=>parts.Keys.Single(key=>key.Contains("_"+image.Tag+".",StringComparison.Ordinal));
        parts[Slot(i)]=Dds(png,mainSize,mainSize,mipmaps);
        parts[Slot(j)]=Dds(png,wideWidth,wideHeight,mipmaps);
        if(allSlots)foreach(var image in native.Images.Where(image=>image.Tag[0] is 'l' or 'e' or 'd'))
            parts[Slot(image)]=Dds(png,image.Header.Width,image.Header.Height,false);
        var rebuilt=XshFolder.Build(parts["xsh.meta"],name=>parts.GetValueOrDefault(name),out error)??throw new InvalidDataException(error);
        var verify=SshFile.Read(rebuilt);
        foreach(var image in verify.Images) {
            var decoded=verify.Decode(image);
            if(decoded.Bgra.Length!=image.Header.Width*image.Header.Height*4)throw new InvalidDataException("Native decode length mismatch");
        }
        if(verify.Images.Single(x=>x.Tag==i.Tag).Header.Width!=mainSize || verify.Images.Single(x=>x.Tag==i.Tag).Header.Mipmaps!=(mipmaps?System.Numerics.BitOperations.Log2((uint)mainSize):0))throw new InvalidDataException("DDS mip metadata did not survive native rebuild");
        return parts;
    }
}
