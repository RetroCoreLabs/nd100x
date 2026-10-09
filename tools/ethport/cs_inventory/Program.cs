// Lists every type, member, enum value and switch case label in C# files,
// one CSV row each, with file and line. Used by the Ethernet II port to
// prove every C# item is accounted for (plan rule R2).
// Usage: cs_inventory <root-dir-for-relative-paths> <file.cs>...
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

static string Csv(string s) =>
    "\"" + System.Text.RegularExpressions.Regex.Replace(s ?? "", @"\s+", " ").Replace("\"", "\"\"") + "\"";

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: cs_inventory <root> <file.cs>...");
    return 2;
}
if (args[0] == "--tests")
{
    // Test mode: one row per method carrying [Test]/[TestCase]/[TestCaseSource].
    // Usage: cs_inventory --tests <root> <file.cs>...
    string troot = Path.GetFullPath(args[1]);
    Console.WriteLine("class,method,file,line,ignore,explicit,cases");
    foreach (string path in args.Skip(2))
    {
        string rel = Path.GetRelativePath(troot, Path.GetFullPath(path)).Replace('\\', '/');
        var tree = CSharpSyntaxTree.ParseText(File.ReadAllText(path), path: path);
        foreach (var m in tree.GetRoot().DescendantNodes().OfType<MethodDeclarationSyntax>())
        {
            var names = m.AttributeLists.SelectMany(a => a.Attributes)
                         .Select(a => a.Name.ToString().Split('.').Last()).ToList();
            if (!names.Any(n => n is "Test" or "TestCase" or "TestCaseSource"))
                continue;
            var clsNode = m.Ancestors().OfType<ClassDeclarationSyntax>().First();
            var cls = clsNode.Identifier.Text;
            var classAttrs = clsNode.AttributeLists.SelectMany(a => a.Attributes)
                                    .Select(a => a.Name.ToString().Split('.').Last()).ToList();
            names.AddRange(classAttrs.Where(n => n is "Ignore" or "Explicit"));
            int line = m.Identifier.GetLocation().GetLineSpan().StartLinePosition.Line + 1;
            Console.WriteLine(string.Join(",", cls, m.Identifier.Text, rel, line,
                names.Contains("Ignore") ? "1" : "0", names.Contains("Explicit") ? "1" : "0",
                names.Count(n => n == "TestCase")));
        }
    }
    return 0;
}
string root = Path.GetFullPath(args[0]);
Console.WriteLine("id,file,line,kind,container,name,detail");
foreach (string path in args.Skip(1))
{
    string rel = Path.GetRelativePath(root, Path.GetFullPath(path)).Replace('\\', '/');
    var tree = CSharpSyntaxTree.ParseText(File.ReadAllText(path), path: path);
    var diags = tree.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
    if (diags.Count > 0)
    {
        Console.Error.WriteLine($"{rel}: {diags.Count} parse errors, first: {diags[0]}");
        return 1;
    }
    foreach (var node in tree.GetRoot().DescendantNodes())
    {
        string kind = null, name = null, detail = "";
        switch (node)
        {
            case BaseTypeDeclarationSyntax t:
                kind = t is EnumDeclarationSyntax ? "enum" : ((TypeDeclarationSyntax)t).Keyword.Text; name = t.Identifier.Text; break;
            case DelegateDeclarationSyntax d:
                kind = "delegate"; name = d.Identifier.Text; detail = d.ParameterList.ToString(); break;
            case MethodDeclarationSyntax m:
                kind = "method"; name = m.Identifier.Text; detail = m.ReturnType + " " + m.ParameterList; break;
            case ConstructorDeclarationSyntax c:
                kind = "ctor"; name = c.Identifier.Text; detail = c.ParameterList.ToString(); break;
            case DestructorDeclarationSyntax c:
                kind = "dtor"; name = c.Identifier.Text; break;
            case LocalFunctionStatementSyntax lf:
                kind = "localfunc"; name = lf.Identifier.Text; detail = lf.ParameterList.ToString(); break;
            case OperatorDeclarationSyntax o:
                kind = "operator"; name = o.OperatorToken.Text; detail = o.ParameterList.ToString(); break;
            case PropertyDeclarationSyntax p:
                kind = "property"; name = p.Identifier.Text; detail = p.Type.ToString(); break;
            case IndexerDeclarationSyntax ix:
                kind = "indexer"; name = "this"; detail = ix.ParameterList.ToString(); break;
            case EventDeclarationSyntax e:
                kind = "event"; name = e.Identifier.Text; detail = e.Type.ToString(); break;
            case EventFieldDeclarationSyntax ef:
                foreach (var v in ef.Declaration.Variables)
                    Emit("event", v.Identifier.Text, ef.Declaration.Type.ToString(), v);
                continue;
            case FieldDeclarationSyntax f:
                bool isConst = f.Modifiers.Any(SyntaxKind.ConstKeyword);
                foreach (var v in f.Declaration.Variables)
                    Emit(isConst ? "const" : "field", v.Identifier.Text,
                         f.Declaration.Type + (v.Initializer != null ? " " + v.Initializer.Value : ""), v);
                continue;
            case LocalDeclarationStatementSyntax ld when ld.IsConst:
                foreach (var v in ld.Declaration.Variables)
                    Emit("localconst", v.Identifier.Text,
                         ld.Declaration.Type + (v.Initializer != null ? " " + v.Initializer.Value : ""), v);
                continue;
            case EnumMemberDeclarationSyntax em:
                kind = "enumvalue"; name = em.Identifier.Text;
                detail = em.EqualsValue?.Value.ToString() ?? ""; break;
            case CaseSwitchLabelSyntax cl:
                kind = "case"; name = cl.Value.ToString(); break;
            case CasePatternSwitchLabelSyntax cp:
                kind = "case"; name = cp.Pattern.ToString(); break;
            case DefaultSwitchLabelSyntax:
                kind = "case"; name = "default"; break;
            case SwitchExpressionArmSyntax arm:
                kind = "switcharm"; name = arm.Pattern.ToString(); break;
        }
        if (kind != null)
            Emit(kind, name, detail, node);
    }

    void Emit(string k, string n, string d, SyntaxNode at)
    {
        int line = at.GetLocation().GetLineSpan().StartLinePosition.Line + 1;
        string container = Container(at);
        string id = $"{rel}:{line}:{k}:{n}";
        Console.WriteLine(string.Join(",", Csv(id), Csv(rel), line, Csv(k), Csv(container), Csv(n), Csv(d)));
    }
}
return 0;

static string Container(SyntaxNode n)
{
    var parts = new List<string>();
    for (var p = n.Parent; p != null; p = p.Parent)
    {
        switch (p)
        {
            case BaseTypeDeclarationSyntax t: parts.Add(t.Identifier.Text); break;
            case MethodDeclarationSyntax m: parts.Add(m.Identifier.Text + "()"); break;
            case ConstructorDeclarationSyntax c: parts.Add(c.Identifier.Text + "()"); break;
            case PropertyDeclarationSyntax pr: parts.Add(pr.Identifier.Text); break;
            case LocalFunctionStatementSyntax lf: parts.Add(lf.Identifier.Text + "()"); break;
        }
    }
    parts.Reverse();
    return string.Join(".", parts);
}
