using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.RateLimiting;

var b=WebApplication.CreateBuilder(args);
b.Services.AddRateLimiter(o=>o.AddFixedWindowLimiter("uploads",x=>{x.PermitLimit=30;x.Window=TimeSpan.FromMinutes(1);x.QueueLimit=0;}));
var app=b.Build(); app.UseRateLimiter();
var root=Path.GetFullPath(Environment.GetEnvironmentVariable("CAPTURE_DIR")??"captures");Directory.CreateDirectory(root);
var apiKey=Environment.GetEnvironmentVariable("API_KEY")??throw new InvalidOperationException("Falta API_KEY");
bool Auth(HttpRequest r)=>r.Headers.TryGetValue("X-API-Key",out var k)&&CryptographicOperations.FixedTimeEquals(Encoding.UTF8.GetBytes(k.ToString()),Encoding.UTF8.GetBytes(apiKey));
app.Use(async(c,n)=>{c.Response.Headers["X-Content-Type-Options"]="nosniff";c.Response.Headers["X-Frame-Options"]="DENY";c.Response.Headers["Referrer-Policy"]="no-referrer";await n();});
app.MapGet("/health",()=>Results.Ok(new{status="ok"}));
app.MapPost("/api/upload",async(HttpRequest r)=>{if(!Auth(r))return Results.Unauthorized();if(!r.HasFormContentType)return Results.BadRequest("multipart requerido");var f=(await r.ReadFormAsync()).Files.GetFile("file");if(f is null||f.Length<8||f.Length>15*1024*1024)return Results.BadRequest("archivo invalido");byte[] data;using(var ms=new MemoryStream()){await f.CopyToAsync(ms);data=ms.ToArray();}bool png=data.AsSpan(0,8).SequenceEqual(new byte[]{137,80,78,71,13,10,26,10});bool jpg=data[0]==0xff&&data[1]==0xd8;if(!png&&!jpg)return Results.BadRequest("solo PNG/JPEG");var id=Convert.ToHexString(RandomNumberGenerator.GetBytes(10)).ToLowerInvariant();var ext=png?"png":"jpg";await File.WriteAllBytesAsync(Path.Combine(root,$"{id}.{ext}"),data);var baseUrl=Environment.GetEnvironmentVariable("PUBLIC_BASE_URL")?.TrimEnd('/')??$"{r.Scheme}://{r.Host}";return Results.Ok(new{id,url=$"{baseUrl}/i/{id}"});}).RequireRateLimiting("uploads").DisableAntiforgery();
app.MapGet("/i/{id}",(string id)=>{if(id.Length!=20||id.Any(c=>!Uri.IsHexDigit(c)))return Results.NotFound();var file=Directory.EnumerateFiles(root,id+".*").FirstOrDefault();return file is null?Results.NotFound():Results.File(file,Path.GetExtension(file)==".png"?"image/png":"image/jpeg");});
app.MapDelete("/api/{id}",(HttpRequest r,string id)=>{if(!Auth(r))return Results.Unauthorized();var f=Directory.EnumerateFiles(root,id+".*").FirstOrDefault();if(f is null)return Results.NotFound();File.Delete(f);return Results.NoContent();});
app.Run();
