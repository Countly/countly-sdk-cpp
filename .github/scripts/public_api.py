"""Prints the SDK's public C++ API as sorted "<qualified name> <kind> <type> <access>" lines, from clang's AST."""
import json
import sys

RECORDS = {"CXXRecordDecl", "ClassTemplateDecl"}
MEMBERS = {
    "CXXMethodDecl": "method",
    "CXXConstructorDecl": "constructor",
    "CXXDestructorDecl": "destructor",
    "CXXConversionDecl": "conversion",
    "FieldDecl": "field",
    "VarDecl": "variable",
    "FunctionDecl": "function",
    "FunctionTemplateDecl": "function template",
    "TypedefDecl": "typedef",
    "TypeAliasDecl": "alias",
}


def documents(text):
    """Yields each JSON document in text: clang's -ast-dump-filter writes one per matching declaration."""
    decoder = json.JSONDecoder()
    position = 0
    while True:
        while position < len(text) and text[position].isspace():
            position += 1
        if position >= len(text):
            return
        document, position = decoder.raw_decode(text, position)
        yield document


def signature(node):
    """Returns the type of a declaration with the flags that change its contract, such as virtual."""
    if node["kind"] == "FunctionTemplateDecl":
        node = next((child for child in node.get("inner", [])
                     if child.get("kind") in ("FunctionDecl", "CXXMethodDecl")), node)
    flags = [flag for flag in ("virtual", "pure", "explicit", "constexpr") if node.get(flag)]
    if node.get("storageClass") == "static":
        flags.append("static")
    return " ".join([node.get("type", {}).get("qualType", "")] + flags).strip()


def walk(node, scope, access, lines):
    """Adds the public declarations under node, declared in scope with the given default member access."""
    kind = node.get("kind")
    if node.get("isImplicit"):
        return
    member_access = node.get("access", access)
    if kind == "NamespaceDecl":
        for child in node.get("inner", []):
            walk(child, f"{scope}{node.get('name', '')}::", "public", lines)
        return
    if member_access not in ("public", "protected"):
        return
    name = node.get("name")
    if kind in RECORDS and name:
        record = node
        if kind == "ClassTemplateDecl":
            record = next((child for child in node.get("inner", [])
                           if child.get("kind") == "CXXRecordDecl"), {})
        if not record.get("completeDefinition"):
            return
        lines.add(f"{scope}{name} {record.get('tagUsed', 'class')} {member_access}")
        section = "public" if record.get("tagUsed") == "struct" else "private"
        for child in record.get("inner", []):
            if child.get("kind") == "AccessSpecDecl":
                section = child.get("access", section)
            else:
                walk(child, f"{scope}{name}::", section, lines)
    elif kind == "EnumDecl" and name:
        lines.add(f"{scope}{name} enum {member_access}")
        for constant in node.get("inner", []):
            if constant.get("kind") == "EnumConstantDecl":
                lines.add(f"{scope}{name}::{constant['name']} enumerator {member_access}")
    elif kind in MEMBERS and name:
        lines.add(f"{scope}{name} {MEMBERS[kind]} {signature(node)} {member_access}")


def main():
    """Reads the JSON AST of include/countly.hpp, filtered to "cly", from stdin and prints the public API lines."""
    # A set, because a declaration can arrive in more than one document.
    lines = set()
    for document in documents(sys.stdin.read()):
        if document.get("kind") == "NamespaceDecl" and document.get("name") == "cly":
            walk(document, "", "public", lines)
    if not lines:
        sys.exit("No declarations in namespace cly were found in the AST.")
    print("\n".join(sorted(lines)))


if __name__ == "__main__":
    main()
