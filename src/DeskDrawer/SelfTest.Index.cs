using System.Diagnostics;
using System.IO;
using System.Text;

namespace DeskDrawer;

public static partial class SelfTest
{
    /// A big DeskBar index (written here in DeskBar's format) is searched on several cores at once: the same results, in the same order, as one core
    /// finds them. Both times go in the report.
    static void IndexSearchTest(Action<string, bool, string> check)
    {
        string file = Path.Combine(Path.GetTempPath(), "dd-selftest-index-" + Guid.NewGuid().ToString("N")[..6] + ".bin");
        try
        {
            var low = new List<byte>(); var orig = new List<byte>(); var noff = new List<uint>(); var ooff = new List<uint>(); var parent = new List<uint>(); var dir = new List<byte>();
            void Add(uint par, string name, bool d)
            {
                noff.Add((uint)low.Count); ooff.Add((uint)orig.Count); low.AddRange(Encoding.UTF8.GetBytes(Matcher.Fold(name) + "\n")); orig.AddRange(Encoding.UTF8.GetBytes(name + "\n")); parent.Add(par); dir.Add(d ? (byte)1 : (byte)0);
            }
            uint folder = 0x80000000u, seed = 12345;
            for (int i = 0; i < 700000; i++)
            {
                if (i % 500 == 0) { folder = (uint)parent.Count; Add(0x80000000u, $"Folder {i / 500}", true); }
                seed = seed * 1103515245u + 12345u; Add(folder, $"file {(seed >> 8) % 100000:D5} note{(i % 977 == 0 ? " report" : "")}.txt", false);
            }
            byte[] roots = Encoding.UTF8.GetBytes("C:\\Users\\me\0"); uint c = (uint)parent.Count;
            using (var w = new BinaryWriter(File.Create(file)))
            {
                ulong total = 56 + 4ul * (c + 1) * 2 + 4ul * c + c + (ulong)low.Count + (ulong)orig.Count + (ulong)roots.Length;
                w.Write(Encoding.ASCII.GetBytes("DB3IDX2\0")); w.Write(2u); w.Write(c); w.Write(1u); w.Write(0u);
                w.Write((ulong)low.Count); w.Write((ulong)orig.Count); w.Write((ulong)roots.Length); w.Write(total);
                foreach (var x in noff) w.Write(x); w.Write((uint)low.Count); foreach (var x in ooff) w.Write(x); w.Write((uint)orig.Count);
                foreach (var x in parent) w.Write(x); w.Write(dir.ToArray()); w.Write(low.ToArray()); w.Write(orig.ToArray()); w.Write(roots);
            }
            using var ix = SharedIndex.OpenFile(file);
            if (ix == null) { check("file index searched on several cores", false, "could not map the test index"); return; }
            var timing = new StringBuilder(); bool same = true;
            foreach (var q in new[] { "note", "report", "folder 12 note" })
            {
                var m = new Matcher(q); var sw = Stopwatch.StartNew();
                var one = ix.Find(m, 400, CancellationToken.None, 1); long t1 = sw.ElapsedMilliseconds; sw.Restart();
                var all = ix.Find(m, 400, CancellationToken.None); long t2 = sw.ElapsedMilliseconds;
                timing.Append($" {q}: one core {t1} ms, all cores {t2} ms;");
                same &= one.Count > 0 && one.SequenceEqual(all);
            }
            bool cancels; using (var cts = new CancellationTokenSource()) { cts.Cancel(); try { ix.Find(new Matcher("note"), 400, cts.Token); cancels = false; } catch (OperationCanceledException) { cancels = true; } }
            check("file index searched on several cores gives the same results as one", same && cancels, $"({ix.Count} entries;{timing} cancel stops it: {cancels})");
        }
        catch (Exception ex) { check("file index searched on several cores", false, ex.Message); }
        finally { try { File.Delete(file); } catch { } }
    }
}
