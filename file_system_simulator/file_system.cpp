#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <ctime>

using namespace std;

// ============================================================================
// 1. NODE HIERARCHY - Polymorphic File System Components
// ============================================================================

class Node
{
public:
    enum class NodeType
    {
        FILE,
        DIRECTORY
    };

    virtual ~Node() = default;

    // Pure virtual methods
    virtual NodeType getType() const = 0;
    virtual size_t getSize() const = 0;
    virtual string getDetails() const = 0;
    virtual Node *clone() const = 0;

    // Common interface
    const string &getName() const { return name; }
    const string &getPath() const { return path; }

    void setPath(const string &newPath) { path = newPath; }

protected:
    string name;
    string path;

    Node(const string &n) : name(n), path("") {}
};

// ============================================================================
// 2. CONCRETE NODE TYPES
// ============================================================================

class File : public Node
{
private:
    size_t size;
    string content;

public:
    File(const string &n, size_t s = 0) : Node(n), size(s), content("") {}

    NodeType getType() const override { return NodeType::FILE; }
    size_t getSize() const override { return size; }

    string getDetails() const override
    {
        return "FILE | " + name + " | Size: " + to_string(size) + " bytes";
    }

    Node *clone() const override
    {
        File *copy = new File(name, size);
        copy->content = content;
        copy->path = path;
        return copy;
    }

    void writeContent(const string &data)
    {
        content = data;
        size = data.length();
    }

    const string &readContent() const { return content; }
};

class Directory : public Node
{
private:
    map<string, Node *> children;

public:
    Directory(const string &n) : Node(n) {}

    ~Directory()
    {
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            delete it->second;
        }
    }

    NodeType getType() const override { return NodeType::DIRECTORY; }

    size_t getSize() const override
    {
        size_t totalSize = 0;
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            totalSize += it->second->getSize();
        }
        return totalSize;
    }

    string getDetails() const override
    {
        return "DIR  | " + name + " | Items: " + to_string(children.size());
    }

    Node *clone() const override
    {
        Directory *copy = new Directory(name);
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            copy->children[it->first] = it->second->clone();
        }
        copy->path = path;
        return copy;
    }

    // Directory-specific methods
    bool addChild(const string &name, Node *node)
    {
        if (children.find(name) != children.end())
        {
            return false;
        }
        children[name] = node;
        return true;
    }

    bool removeChild(const string &name)
    {
        auto it = children.find(name);
        if (it == children.end())
        {
            return false;
        }
        delete it->second;
        children.erase(it);
        return true;
    }

    Node *getChild(const string &name) const
    {
        auto it = children.find(name);
        return it != children.end() ? it->second : NULL;
    }

    const map<string, Node *> &getChildren() const
    {
        return children;
    }

    bool hasChild(const string &name) const
    {
        return children.find(name) != children.end();
    }
};

// ============================================================================
// 3. FACTORY PATTERN
// ============================================================================

class NodeFactory
{
public:
    static Node *createFile(const string &name, size_t size = 0)
    {
        return new File(name, size);
    }

    static Node *createDirectory(const string &name)
    {
        return new Directory(name);
    }

    static bool isFile(Node *node)
    {
        return node && node->getType() == Node::NodeType::FILE;
    }

    static bool isDirectory(Node *node)
    {
        return node && node->getType() == Node::NodeType::DIRECTORY;
    }
};

// ============================================================================
// 4. VISITOR PATTERN
// ============================================================================

class NodeVisitor
{
public:
    virtual ~NodeVisitor() = default;
    virtual void visit(File *file) = 0;
    virtual void visit(Directory *dir) = 0;
};

class PrintVisitor : public NodeVisitor
{
private:
    int indentLevel;

public:
    PrintVisitor() : indentLevel(0) {}

    void visit(File *file) override
    {
        cout << string(indentLevel * 2, ' ') << "├─ " << file->getDetails() << endl;
    }

    void visit(Directory *dir) override
    {
        cout << string(indentLevel * 2, ' ') << "├─ " << dir->getDetails() << endl;
        indentLevel++;
        for (auto it = dir->getChildren().begin(); it != dir->getChildren().end(); ++it)
        {
            File *file = dynamic_cast<File *>(it->second);
            Directory *subdir = dynamic_cast<Directory *>(it->second);
            if (file)
            {
                visit(file);
            }
            else if (subdir)
            {
                visit(subdir);
            }
        }
        indentLevel--;
    }
};

class SearchVisitor : public NodeVisitor
{
private:
    string searchName;
    vector<string> results;

public:
    SearchVisitor(const string &name) : searchName(name) {}

    void visit(File *file) override
    {
        if (file && file->getName().find(searchName) != string::npos)
        {
            results.push_back(file->getPath());
        }
    }

    void visit(Directory *dir) override
    {
        if (dir && dir->getName().find(searchName) != string::npos)
        {
            results.push_back(dir->getPath());
        }
        if (dir)
        {
            for (auto it = dir->getChildren().begin(); it != dir->getChildren().end(); ++it)
            {
                File *file = dynamic_cast<File *>(it->second);
                Directory *subdir = dynamic_cast<Directory *>(it->second);
                if (file)
                {
                    visit(file);
                }
                else if (subdir)
                {
                    visit(subdir);
                }
            }
        }
    }

    const vector<string> &getResults() const { return results; }
};

class SizeVisitor : public NodeVisitor
{
private:
    size_t totalSize;

public:
    SizeVisitor() : totalSize(0) {}

    void visit(File *file) override
    {
        if (file)
            totalSize += file->getSize();
    }

    void visit(Directory *dir) override
    {
        if (dir)
        {
            for (auto it = dir->getChildren().begin(); it != dir->getChildren().end(); ++it)
            {
                File *file = dynamic_cast<File *>(it->second);
                Directory *subdir = dynamic_cast<Directory *>(it->second);
                if (file)
                {
                    visit(file);
                }
                else if (subdir)
                {
                    visit(subdir);
                }
            }
        }
    }

    size_t getTotalSize() const { return totalSize; }
};

// ============================================================================
// 5. FILE SYSTEM CLASS
// ============================================================================

class FileSystem
{
private:
    Directory *root;
    Directory *currentDir;

    // Helper: Parse path into components
    vector<string> splitPath(const string &path)
    {
        vector<string> parts;
        stringstream ss(path);
        string part;
        while (getline(ss, part, '/'))
        {
            if (!part.empty())
            {
                parts.push_back(part);
            }
        }
        return parts;
    }

    // Helper: Navigate to directory
    Directory *navigatePath(const string &path)
    {
        if (path.empty() || path == "/")
            return root;

        vector<string> parts = splitPath(path);
        Directory *current = (path[0] == '/') ? root : currentDir;

        for (size_t i = 0; i < parts.size(); ++i)
        {
            const string &part = parts[i];
            if (part.empty() || part == ".")
                continue;
            if (part == "..")
                continue;

            Node *child = current->getChild(part);
            if (!child)
                return NULL;

            Directory *subdir = dynamic_cast<Directory *>(child);
            if (subdir)
            {
                current = subdir;
            }
            else
            {
                return NULL;
            }
        }
        return current;
    }

    string buildPath(const string &dir, const string &name)
    {
        if (dir.empty() || dir == "/")
            return "/" + name;
        return dir + "/" + name;
    }

public:
    FileSystem()
    {
        root = (Directory *)NodeFactory::createDirectory("/");
        currentDir = root;
    }

    ~FileSystem()
    {
        delete root;
    }

    // Basic Operations
    bool createFile(const string &path, size_t size = 0)
    {
        vector<string> parts = splitPath(path);
        if (parts.empty())
            return false;

        string fileName = parts.back();
        string dirPath = path.substr(0, path.rfind(fileName));

        Directory *targetDir = navigatePath(dirPath);
        if (!targetDir)
            return false;

        Node *newFile = NodeFactory::createFile(fileName, size);
        newFile->setPath(buildPath(dirPath, fileName));
        return targetDir->addChild(fileName, newFile);
    }

    bool createDirectory(const string &path)
    {
        vector<string> parts = splitPath(path);
        if (parts.empty())
            return false;

        string dirName = parts.back();
        string parentPath = path.substr(0, path.rfind(dirName));

        Directory *parentDir = navigatePath(parentPath);
        if (!parentDir)
            return false;

        Node *newDir = NodeFactory::createDirectory(dirName);
        newDir->setPath(buildPath(parentPath, dirName));
        return parentDir->addChild(dirName, newDir);
    }

    bool deleteNode(const string &path)
    {
        vector<string> parts = splitPath(path);
        if (parts.empty())
            return false;

        string nodeName = parts.back();
        string parentPath = path.substr(0, path.rfind(nodeName));

        Directory *parentDir = navigatePath(parentPath);
        if (!parentDir)
            return false;

        return parentDir->removeChild(nodeName);
    }

    bool copyNode(const string &srcPath, const string &destPath)
    {
        string srcDirPath = srcPath.substr(0, srcPath.rfind('/'));
        Directory *srcDir = navigatePath(srcDirPath);
        if (!srcDir)
            return false;

        string srcName = srcPath.substr(srcPath.rfind('/') + 1);
        Node *srcNode = srcDir->getChild(srcName);
        if (!srcNode)
            return false;

        Directory *destDir = navigatePath(destPath);
        if (!destDir)
            return false;

        Node *copiedNode = srcNode->clone();
        return destDir->addChild(srcName, copiedNode);
    }

    bool moveNode(const string &srcPath, const string &destPath)
    {
        if (!copyNode(srcPath, destPath))
            return false;
        return deleteNode(srcPath);
    }

    // Visitor-based operations
    void printTree()
    {
        cout << "\n=== FILE SYSTEM TREE ===" << endl;
        PrintVisitor printer;
        printer.visit(root);
    }

    vector<string> searchNode(const string &name)
    {
        SearchVisitor searcher(name);
        searcher.visit(root);
        return searcher.getResults();
    }

    size_t getTotalSize()
    {
        SizeVisitor sizer;
        sizer.visit(root);
        return sizer.getTotalSize();
    }

    Directory *getRoot() const { return root; }
};

// ============================================================================
// 6. TEST HARNESS
// ============================================================================

void demonstrateFileSystem()
{
    cout << "\n╔════════════════════════════════════════════════════╗" << endl;
    cout << "║      FILE SYSTEM SIMULATOR DEMONSTRATION            ║" << endl;
    cout << "╚════════════════════════════════════════════════════╝" << endl;

    FileSystem fs;

    // Create directory structure
    cout << "\n→ Creating directories..." << endl;
    fs.createDirectory("/home");
    fs.createDirectory("/home/user");
    fs.createDirectory("/home/user/documents");
    fs.createDirectory("/home/user/downloads");
    cout << "✓ Directories created" << endl;

    // Create files
    cout << "\n→ Creating files..." << endl;
    fs.createFile("/home/user/documents/resume.txt", 2048);
    fs.createFile("/home/user/documents/portfolio.pdf", 5120);
    fs.createFile("/home/user/downloads/image.jpg", 3072);
    fs.createFile("/home/user/.bashrc", 512);
    cout << "✓ Files created" << endl;

    // Display tree
    fs.printTree();

    // Search operations
    cout << "\n=== SEARCH OPERATIONS ===" << endl;
    vector<string> searchResults = fs.searchNode("documents");
    cout << "Search for 'documents': " << searchResults.size() << " results found" << endl;
    for (size_t i = 0; i < searchResults.size(); ++i)
    {
        cout << "  → " << searchResults[i] << endl;
    }

    // Size calculations
    cout << "\n=== SIZE STATISTICS ===" << endl;
    size_t totalSize = fs.getTotalSize();
    cout << "Total size: " << totalSize << " bytes (" << (totalSize / 1024.0) << " KB)" << endl;

    // Copy operation
    cout << "\n→ Copying files..." << endl;
    fs.copyNode("/home/user/documents/resume.txt", "/home/user/downloads/");
    cout << "✓ File copied to downloads" << endl;

    // Display updated tree
    cout << "\nAfter copy operation:" << endl;
    fs.printTree();

    // Delete operation
    cout << "\n→ Deleting files..." << endl;
    fs.deleteNode("/home/user/downloads/image.jpg");
    cout << "✓ File deleted" << endl;

    // Final tree
    cout << "\nFinal file system state:" << endl;
    fs.printTree();

    cout << "\n=== STATISTICS ===" << endl;
    cout << "Final total size: " << fs.getTotalSize() << " bytes" << endl;
    cout << "\n✓ Demonstration completed successfully!" << endl;
}

// ============================================================================
// 7. MAIN ENTRY POINT
// ============================================================================

int main()
{
    try
    {
        demonstrateFileSystem();
    }
    catch (const exception &e)
    {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    return 0;
}
