using AnimeStudio;
using System.Text.Json;
using System.Reflection;

// Read-only, targeted native prefab inspector. Game resources stay local.
if(args.Length!=2) throw new ArgumentException("LaevatInspector <bundle-directory> <output-directory>");
var destination=Path.GetFullPath(args[1]);Directory.CreateDirectory(destination);
foreach(ClassIDType type in Enum.GetValues<ClassIDType>()) TypeFlags.SetType(type,false,false);
foreach(var type in new[]{ClassIDType.Mesh,ClassIDType.Material,ClassIDType.GameObject,ClassIDType.Transform,ClassIDType.SkinnedMeshRenderer,ClassIDType.MonoBehaviour,ClassIDType.AssetBundle}) TypeFlags.SetType(type,true,false);
var manager=new AssetsManager { Game=GameManager.GetGameByType(GameType.ArknightsEndfield)??throw new Exception("No Endfield reader") };
manager.LoadFiles(Directory.GetFiles(Path.GetFullPath(args[0]),"*.ab",SearchOption.AllDirectories));
var records=new List<object>();
foreach(var bundle in manager.assetsFileList.SelectMany(f=>f.Objects).OfType<AssetBundle>())
foreach(var entry in bundle.m_Container)
 if(entry.Key.EndsWith("weaponcommonconfig.asset",StringComparison.OrdinalIgnoreCase)&&entry.Value.asset.TryGet(out var config))File.WriteAllText(Path.Combine(destination,"weapon-common-config-tree.txt"),config.Dump());
foreach(var file in manager.assetsFileList)
foreach(var obj in file.Objects)
{
 if(obj is Mesh mesh && mesh.m_Name=="S_wpn_misc_00016_02_lod0")
 {
  File.WriteAllText(Path.Combine(destination,"blade-mesh-tree.txt"),mesh.Dump());
  var fields=typeof(Mesh).GetFields(BindingFlags.Public|BindingFlags.Instance);
  var local=new Dictionary<string,object?>();
  foreach(var field in fields)
  {
   if(field.Name.Contains("Vertices")||field.Name.Contains("LocalAABB")||field.Name.Contains("VertexData"))local[field.Name]=field.GetValue(mesh);
  }
  File.WriteAllText(Path.Combine(destination,"blade-geometry.json"),JsonSerializer.Serialize(local,new JsonSerializerOptions{WriteIndented=true,IncludeFields=true}));
  records.Add(new{kind="Mesh",name=mesh.m_Name,pathId=mesh.m_PathID,file=file.fileName,fields=fields.Select(x=>x.Name).ToArray()});
 }
 if(obj is GameObject go && go.m_Name=="wpn_misc_0016") File.WriteAllText(Path.Combine(destination,"weapon-prefab-tree.txt"),go.Dump());
 if(obj is MonoBehaviour behaviour && behaviour.m_GameObject.TryGet(out var owner) && (owner.m_Name=="wpn_misc_0016"||owner.m_Name=="chr_0016_laevat_postmodel")) File.WriteAllText(Path.Combine(destination,$"{owner.m_Name}-component-{obj.m_PathID}.txt"),obj.Dump());
 if(obj is SkinnedMeshRenderer renderer && renderer.m_GameObject.TryGet(out var gameObject) && gameObject.m_Name=="S_wpn_misc_00016_02_lod0")File.WriteAllText(Path.Combine(destination,"blade-renderer-tree.txt"),renderer.Dump());
}
File.WriteAllText(Path.Combine(destination,"inspection.json"),JsonSerializer.Serialize(records,new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine($"mesh records={records.Count}");
