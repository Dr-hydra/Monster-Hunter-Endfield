using AnimeStudio;
using System.Text.Json;
using System.Reflection;
using System.Text.RegularExpressions;

// Targeted offline geometry export. No installed resources or source objects
// are modified; generated meshes and decoded game data stay local.
if(args.Length < 2) throw new ArgumentException("EndfieldWeaponInspector <bundle-directory> <output-directory> [mesh-regex]");
var destination=Path.GetFullPath(args[1]);Directory.CreateDirectory(destination);
var selector=new Regex(args.Length>2?args[2]:".*lod0$",RegexOptions.IgnoreCase);
var animations=args.Contains("--animations");
foreach(ClassIDType type in Enum.GetValues<ClassIDType>()) TypeFlags.SetType(type,false,false);
foreach(var type in new[]{ClassIDType.Mesh,ClassIDType.Material,ClassIDType.GameObject,ClassIDType.Transform,ClassIDType.MeshRenderer,ClassIDType.MeshFilter,ClassIDType.SkinnedMeshRenderer,ClassIDType.MonoBehaviour,ClassIDType.AssetBundle}) TypeFlags.SetType(type,true,false);
if(animations)TypeFlags.SetType(ClassIDType.AnimationClip,true,false);
var manager=new AssetsManager { Game=GameManager.GetGameByType(GameType.ArknightsEndfield)??throw new Exception("No Endfield reader") };
manager.LoadFiles(Directory.GetFiles(Path.GetFullPath(args[0]),"*.ab",SearchOption.AllDirectories));
File.WriteAllText(Path.Combine(destination,"object-type-counts.json"),JsonSerializer.Serialize(manager.assetsFileList.SelectMany(f=>f.Objects).GroupBy(o=>o.type.ToString()).ToDictionary(g=>g.Key,g=>g.Count())));
var records=new List<object>();
var staticRecords=new List<object>();
string Identity(AnimeStudio.Object obj)=>obj.assetsFile.fileName.ToLowerInvariant()+":"+obj.m_PathID;
object Reference<T>(PPtr<T> p) where T:AnimeStudio.Object => p.TryGet(out var o)?new{id=Identity(o)}:new{id=(string?)null};
var options=new JsonSerializerOptions{IncludeFields=true};
foreach(var file in manager.assetsFileList)
foreach(var obj in file.Objects)
{
 if(animations && obj is AnimationClip animation && selector.IsMatch(animation.m_Name))
 {
  string identity=file.fileName.ToLowerInvariant()+"_"+obj.m_PathID;
  var fields=typeof(AnimationClip).GetFields(BindingFlags.Public|BindingFlags.Instance|BindingFlags.DeclaredOnly);
  File.WriteAllText(Path.Combine(destination,"animation-"+identity+".txt"),obj.Dump());
  File.WriteAllBytes(Path.Combine(destination,"animation-"+identity+".raw"),obj.GetRawData());
  var selected=fields.Where(f=>!f.Name.Contains("TypeTree")).ToDictionary(f=>f.Name,f=>f.GetValue(animation));
  File.WriteAllText(Path.Combine(destination,"animation-"+identity+".json"),JsonSerializer.Serialize(selected,options));
  records.Add(new{kind="AnimationClip",name=animation.m_Name,id=Identity(obj),file="animation-"+identity+".json",fields=fields.Select(f=>f.Name).ToArray()});
 }
 if(obj is MeshRenderer staticRenderer)staticRecords.Add(new{id=Identity(obj),type="MeshRenderer",game_object=Reference(staticRenderer.m_GameObject),materials=staticRenderer.m_Materials.Select(p=>Reference(p)).ToArray()});
 if(obj is MeshFilter filter)staticRecords.Add(new{id=Identity(obj),type="MeshFilter",game_object=Reference(filter.m_GameObject),mesh=Reference(filter.m_Mesh)});
 if(obj is Mesh mesh && selector.IsMatch(mesh.m_Name))
 {
  string identity=file.fileName.ToLowerInvariant()+"_"+obj.m_PathID;
  var fields=typeof(Mesh).GetFields(BindingFlags.Public|BindingFlags.Instance);
  var local=new Dictionary<string,object?>{{"name",mesh.m_Name},{"id",file.fileName.ToLowerInvariant()+":"+obj.m_PathID}};
  foreach(var field in fields)
   if(field.Name.Contains("Vertices")||field.Name.Contains("Indices")||field.Name.Contains("Skin")||field.Name.Contains("UV0")||field.Name.Contains("Normals")||field.Name.Contains("BindPose")||field.Name.Contains("BoneNameHashes")||field.Name.Contains("LocalAABB")) local[field.Name]=field.GetValue(mesh);
  File.WriteAllText(Path.Combine(destination,identity+".json"),JsonSerializer.Serialize(local,options));
  // Exact Unity eRC fields disambiguate the backend's flattened matrix layout.
  var dump=mesh.Dump();
  if(dump!=null){int start=dump.IndexOf("vector m_BindPose",StringComparison.Ordinal);int end=start<0?-1:dump.IndexOf("vector m_BoneNameHashes",start,StringComparison.Ordinal);if(end>start)File.WriteAllText(Path.Combine(destination,identity+".bindpose.txt"),dump[start..end]);}
  records.Add(new{kind="Mesh",name=mesh.m_Name,id=local["id"],file=identity+".json",fields=fields.Select(x=>x.Name).ToArray()});
 }
 if(obj is MonoBehaviour behaviour && behaviour.m_GameObject.TryGet(out var owner) && (owner.m_Name.StartsWith("wpn_",StringComparison.OrdinalIgnoreCase)||owner.m_Name.StartsWith("S_wpn_",StringComparison.OrdinalIgnoreCase)))
  File.WriteAllText(Path.Combine(destination,"component-"+owner.m_Name+"-"+obj.m_PathID+".txt"),obj.Dump());
 if(obj is MonoBehaviour standalone && (standalone.m_Name.StartsWith("data_chr_") || standalone.m_Name.Contains("laevat") || standalone.m_Name.Contains("typhoea") || standalone.m_Name.Contains("VFX_character")))
  File.WriteAllText(Path.Combine(destination,"asset-"+standalone.m_Name+"-"+obj.m_PathID+".txt"),obj.Dump());
 if(obj is MonoBehaviour characterData && characterData.m_Name.StartsWith("data_chr_")){
  File.WriteAllBytes(Path.Combine(destination,"asset-"+characterData.m_Name+".raw"),obj.GetRawData());
  var refFields=file.GetType().GetFields(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance).Where(f=>f.Name.Contains("RefType")||f.Name.Contains("refType")).ToDictionary(f=>f.Name,f=>f.GetValue(file));
  File.WriteAllText(Path.Combine(destination,"asset-"+characterData.m_Name+".reftypes.json"),JsonSerializer.Serialize(refFields,options));
  File.WriteAllText(Path.Combine(destination,"asset-"+characterData.m_Name+".nodes.json"),JsonSerializer.Serialize(obj.serializedType.m_Type.m_Nodes,options));
 }
}
File.WriteAllText(Path.Combine(destination,"inspection.json"),JsonSerializer.Serialize(records,options));
File.WriteAllText(Path.Combine(destination,"static-renderers.json"),JsonSerializer.Serialize(staticRecords,options));
Console.WriteLine($"exported meshes={records.Count}");
manager.Clear();
