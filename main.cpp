#include <pistache/endpoint.h>
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <zstd.h>

using namespace Pistache;
using namespace std;

#pragma pack(1)

struct Direccion {
    uint32_t departamento;
    uint32_t provincia;
    uint32_t ciudad;
    uint32_t distrito;
    uint32_t ubicacion;
};

class Ciudadano {
private:
    char dni[8];
    uint32_t nombres;
    uint32_t apellidos;
    uint32_t lugar_nacimiento;
    Direccion direccion;
    uint64_t telefono;
    uint32_t correo;
    char nacionalidad[2];
    unsigned sexo : 1;
    unsigned estado_civil : 3;

public:
    Ciudadano(const char* dni, uint32_t nombres, uint32_t apellidos, uint32_t lugar_nacimiento, Direccion direccion, uint64_t telefono, uint32_t correo, const char* nacionalidad, unsigned sexo, unsigned estado_civil)
        : nombres(nombres), apellidos(apellidos), lugar_nacimiento(lugar_nacimiento), direccion(direccion), telefono(telefono), correo(correo), sexo(sexo), estado_civil(estado_civil)
    {
        strncpy(this->dni, dni, 8);
        strncpy(this->nacionalidad, nacionalidad, 2);
    }

    string getDni() const { return string(dni, 8); }
    uint32_t getNombres() const { return nombres; }
    uint32_t getApellidos() const { return apellidos; }
    uint32_t getLugarNacimiento() const { return lugar_nacimiento; }
    Direccion getDireccion() const { return direccion; }
    uint64_t getTelefono() const { return telefono; }
    uint32_t getCorreo() const { return correo; }
    string getNacionalidad() const { return string(nacionalidad, 2); }
    unsigned getSexo() const { return sexo; }
    unsigned getEstadoCivil() const { return estado_civil; }

    void serialize(ostringstream& os) const {
        os.write(dni, 8);
        os.write(reinterpret_cast<const char*>(&nombres), sizeof(nombres));
        os.write(reinterpret_cast<const char*>(&apellidos), sizeof(apellidos));
        os.write(reinterpret_cast<const char*>(&lugar_nacimiento), sizeof(lugar_nacimiento));
        os.write(reinterpret_cast<const char*>(&direccion), sizeof(direccion));
        os.write(reinterpret_cast<const char*>(&telefono), sizeof(telefono));
        os.write(reinterpret_cast<const char*>(&correo), sizeof(correo));
        os.write(nacionalidad, 2);
        os.put((sexo << 3) | estado_civil);
    }

    static Ciudadano deserialize(istringstream& is) {
        char dni[8];
        is.read(dni, 8);

        uint32_t nombres;
        is.read(reinterpret_cast<char*>(&nombres), sizeof(nombres));

        uint32_t apellidos;
        is.read(reinterpret_cast<char*>(&apellidos), sizeof(apellidos));

        uint32_t lugar_nacimiento;
        is.read(reinterpret_cast<char*>(&lugar_nacimiento), sizeof(lugar_nacimiento));

        Direccion direccion;
        is.read(reinterpret_cast<char*>(&direccion), sizeof(direccion));

        uint64_t telefono;
        is.read(reinterpret_cast<char*>(&telefono), sizeof(telefono));

        uint32_t correo;
        is.read(reinterpret_cast<char*>(&correo), sizeof(correo));

        char nacionalidad[2];
        is.read(nacionalidad, 2);

        char sex_and_status;
        is.get(sex_and_status);

        unsigned sexo = (sex_and_status >> 3) & 1;
        unsigned estado_civil = sex_and_status & 7;

        return Ciudadano(dni, nombres, apellidos, lugar_nacimiento, direccion, telefono, correo, nacionalidad, sexo, estado_civil);
    }
};

class BTreeNode {
public:
    BTreeNode(int t, bool leaf);

    void traverse() const;
    void splitChild(int i, BTreeNode* y);
    void insertNonFull(Ciudadano* citizen);
    Ciudadano* search(const string& dni) const;
    void remove(const string& dni);
    void removeFromLeaf(int idx);
    void removeFromNonLeaf(int idx);
    Ciudadano getPredecessor(int idx);
    Ciudadano getSuccessor(int idx);
    void fill(int idx);
    void borrowFromPrev(int idx);
    void borrowFromNext(int idx);
    void merge(int idx);
    void serialize(ostringstream& buffer) const;
    void deserialize(istringstream& buffer);

    friend class Btree;

private:
    int t;
    int n;
    bool leaf;
    vector<Ciudadano*> keys;
    vector<BTreeNode*> children;
};

class Btree {
public:
    Btree(int t) : t(t) { root = nullptr; }

    void traverse() const {
        if (root)
            root->traverse();
    }

    void insert(Ciudadano* citizen);
    Ciudadano* search(const string& dni) const;
    void remove(const string& dni);

    bool serialize(const string& filename) const;
    bool deserialize(const string& filename);

    void serialize_string_pool(ostringstream& buffer) const;
    void deserialize_string_pool(istringstream& buffer);

    string get_string_from_pool(uint32_t index) const {
        return pool_strings[index];
    }

    uint32_t get_pool_index(const string& str) {
        if (string_pool.find(str) == string_pool.end()) {
            string_pool[str] = pool_strings.size();
            pool_strings.push_back(str);
        }
        return string_pool[str];
    }

private:
    BTreeNode* root;
    int t;
    unordered_map<string, uint32_t> string_pool;
    vector<string> pool_strings;
};

BTreeNode::BTreeNode(int t, bool leaf) : t(t), n(0), leaf(leaf) {
    keys.resize(2 * t - 1);
    children.resize(2 * t);
}

void BTreeNode::traverse() const {
    int i;
    for (i = 0; i < n; i++) {
        if (!leaf)
            children[i]->traverse();
        cout << keys[i]->getDni() << endl;
    }
    if (!leaf)
        children[i]->traverse();
}

void BTreeNode::insertNonFull(Ciudadano* citizen) {
    int i = n - 1;

    if (leaf) {
        while (i >= 0 && keys[i]->getDni() > citizen->getDni()) {
            keys[i + 1] = keys[i];
            i--;
        }
        keys[i + 1] = citizen;
        n++;
    } else {
        while (i >= 0 && keys[i]->getDni() > citizen->getDni())
            i--;

        if (children[i + 1]->n == 2 * t - 1) {
            splitChild(i + 1, children[i + 1]);

            if (keys[i + 1]->getDni() < citizen->getDni())
                i++;
        }
        children[i + 1]->insertNonFull(citizen);
    }
}

void BTreeNode::splitChild(int i, BTreeNode* y) {
    BTreeNode* z = new BTreeNode(y->t, y->leaf);
    z->n = t - 1;

    for (int j = 0; j < t - 1; j++)
        z->keys[j] = y->keys[j + t];

    if (!y->leaf) {
        for (int j = 0; j < t; j++)
            z->children[j] = y->children[j + t];
    }

    y->n = t - 1;

    for (int j = n; j >= i + 1; j--)
        children[j + 1] = children[j];

    children[i + 1] = z;

    for (int j = n - 1; j >= i; j--)
        keys[j + 1] = keys[j];

    keys[i] = y->keys[t - 1];
    n++;
}

Ciudadano* BTreeNode::search(const string& dni) const {
    int i = 0;
    while (i < n && dni > keys[i]->getDni())
        i++;

    if (i < n && dni == keys[i]->getDni())
        return keys[i];

    if (leaf)
        return nullptr;

    return children[i]->search(dni);
}

void BTreeNode::remove(const string& dni) {
    int idx = 0;
    while (idx < n && keys[idx]->getDni() < dni)
        ++idx;

    if (idx < n && keys[idx]->getDni() == dni) {
        if (leaf)
            removeFromLeaf(idx);
        else
            removeFromNonLeaf(idx);
    } else {
        if (leaf) {
            cout << "The key " << dni << " does not exist in the tree\n";
            return;
        }

        bool flag = (idx == n);

        if (children[idx]->n < t)
            fill(idx);

        if (flag && idx > n)
            children[idx - 1]->remove(dni);
        else
            children[idx]->remove(dni);
    }
}

void BTreeNode::removeFromLeaf(int idx) {
    for (int i = idx + 1; i < n; ++i)
        keys[i - 1] = keys[i];
    n--;
}

void BTreeNode::removeFromNonLeaf(int idx) {
    Ciudadano* k = keys[idx];

    if (children[idx]->n >= t) {
        Ciudadano pred = getPredecessor(idx);
        keys[idx] = new Ciudadano(pred);
        children[idx]->remove(pred.getDni());
    } else if (children[idx + 1]->n >= t) {
        Ciudadano succ = getSuccessor(idx);
        keys[idx] = new Ciudadano(succ);
        children[idx + 1]->remove(succ.getDni());
    } else {
        merge(idx);
        children[idx]->remove(k->getDni());
    }
    delete k;
}

Ciudadano BTreeNode::getPredecessor(int idx) {
    BTreeNode* cur = children[idx];
    while (!cur->leaf)
        cur = cur->children[cur->n];
    return *cur->keys[cur->n - 1];
}

Ciudadano BTreeNode::getSuccessor(int idx) {
    BTreeNode* cur = children[idx + 1];
    while (!cur->leaf)
        cur = cur->children[0];
    return *cur->keys[0];
}

void BTreeNode::fill(int idx) {
    if (idx != 0 && children[idx - 1]->n >= t)
        borrowFromPrev(idx);
    else if (idx != n && children[idx + 1]->n >= t)
        borrowFromNext(idx);
    else {
        if (idx != n)
            merge(idx);
        else
            merge(idx - 1);
    }
}

void BTreeNode::borrowFromPrev(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx - 1];

    for (int i = child->n - 1; i >= 0; --i)
        child->keys[i + 1] = child->keys[i];

    if (!child->leaf) {
        for (int i = child->n; i >= 0; --i)
            child->children[i + 1] = child->children[i];
    }

    child->keys[0] = keys[idx - 1];

    if (!leaf)
        child->children[0] = sibling->children[sibling->n];

    keys[idx - 1] = sibling->keys[sibling->n - 1];

    child->n += 1;
    sibling->n -= 1;
}

void BTreeNode::borrowFromNext(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx + 1];

    child->keys[(child->n)] = keys[idx];

    if (!(child->leaf))
        child->children[(child->n) + 1] = sibling->children[0];

    keys[idx] = sibling->keys[0];

    for (int i = 1; i < sibling->n; ++i)
        sibling->keys[i - 1] = sibling->keys[i];

    if (!sibling->leaf) {
        for (int i = 1; i <= sibling->n; ++i)
            sibling->children[i - 1] = sibling->children[i];
    }

    child->n += 1;
    sibling->n -= 1;
}

void BTreeNode::merge(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx + 1];

    child->keys[t - 1] = keys[idx];

    for (int i = 0; i < sibling->n; ++i)
        child->keys[i + t] = sibling->keys[i];

    if (!child->leaf) {
        for (int i = 0; i <= sibling->n; ++i)
            child->children[i + t] = sibling->children[i];
    }

    for (int i = idx + 1; i < n; ++i)
        keys[i - 1] = keys[i];

    for (int i = idx + 2; i <= n; ++i)
        children[i - 1] = children[i];

    child->n += sibling->n + 1;
    n--;

    delete sibling;
}

void BTreeNode::serialize(ostringstream& buffer) const {
    buffer.write(reinterpret_cast<const char*>(&n), sizeof(n));
    buffer.write(reinterpret_cast<const char*>(&leaf), sizeof(leaf));
    for (int i = 0; i < n; i++) {
        keys[i]->serialize(buffer);
    }
    if (!leaf) {
        for (int i = 0; i <= n; i++) {
            children[i]->serialize(buffer);
        }
    }
}

void BTreeNode::deserialize(istringstream& buffer) {
    buffer.read(reinterpret_cast<char*>(&n), sizeof(n));
    buffer.read(reinterpret_cast<char*>(&leaf), sizeof(leaf));
    keys.resize(2 * t - 1);
    children.resize(2 * t);
    for (int i = 0; i < n; i++) {
        keys[i] = new Ciudadano(Ciudadano::deserialize(buffer));
    }
    if (!leaf) {
        for (int i = 0; i <= n; i++) {
            children[i] = new BTreeNode(t, true);
            children[i]->deserialize(buffer);
        }
    }
}

void Btree::insert(Ciudadano* citizen) {
    if (!root) {
        root = new BTreeNode(t, true);
        root->keys[0] = citizen;
        root->n = 1;
    } else {
        if (root->n == 2 * t - 1) {
            BTreeNode* s = new BTreeNode(t, false);
            s->children[0] = root;
            s->splitChild(0, root);

            int i = 0;
            if (s->keys[0]->getDni() < citizen->getDni())
                i++;
            s->children[i]->insertNonFull(citizen);

            root = s;
        } else {
            root->insertNonFull(citizen);
        }
    }
}

Ciudadano* Btree::search(const string& dni) const {
    if (!root) {
        cout << "Tree is empty" << endl;
        return nullptr;
    }
    return root->search(dni);
}

void Btree::remove(const string& dni) {
    if (!root) {
        cout << "The tree is empty\n";
        return;
    }

    root->remove(dni);

    if (root->n == 0) {
        BTreeNode* tmp = root;
        if (root->leaf)
            root = nullptr;
        else
            root = root->children[0];
        delete tmp;
    }
}

void Btree::serialize_string_pool(ostringstream& buffer) const {
    uint32_t pool_size = pool_strings.size();
    buffer.write(reinterpret_cast<const char*>(&pool_size), sizeof(pool_size));
    for (const auto& str : pool_strings) {
        uint32_t str_size = str.size();
        buffer.write(reinterpret_cast<const char*>(&str_size), sizeof(str_size));
        buffer.write(str.data(), str_size);
    }
}

void Btree::deserialize_string_pool(istringstream& buffer) {
    uint32_t pool_size;
    buffer.read(reinterpret_cast<char*>(&pool_size), sizeof(pool_size));
    pool_strings.resize(pool_size);
    for (uint32_t i = 0; i < pool_size; ++i) {
        uint32_t str_size;
        buffer.read(reinterpret_cast<char*>(&str_size), sizeof(str_size));
        string str(str_size, '\0');
        buffer.read(&str[0], str_size);
        pool_strings[i] = str;
        string_pool[str] = i;
    }
}

bool Btree::serialize(const string& filename) const {
    ostringstream buffer;
    if (root) {
        auto start = chrono::high_resolution_clock::now();
        root->serialize(buffer);
        serialize_string_pool(buffer);
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
        cout << "B-Tree serializado en buffer en " << duration.count() << " milisegundos." << endl;

        string uncompressed_data = buffer.str();
        size_t compressed_size = ZSTD_compressBound(uncompressed_data.size());
        vector<char> compressed_data(compressed_size);
        size_t actual_compressed_size = ZSTD_compress(compressed_data.data(), compressed_size, uncompressed_data.data(), uncompressed_data.size(), 1);

        ofstream file(filename, ios::binary | ios::out);
        if (file.is_open()) {
            file.write(compressed_data.data(), actual_compressed_size);
            file.close();
            cout << "B-Tree serializado y comprimido en archivo con exito." << endl;
            return true;
        } else {
            cerr << "Error abriendo archivo para serializacion." << endl;
            return false;
        }
    } else {
        cerr << "B-Tree está vacío." << endl;
        return false;
    }
}

bool Btree::deserialize(const string& filename) {
    ifstream file(filename, ios::binary | ios::in);
    if (file.is_open()) {
        auto start = chrono::high_resolution_clock::now();

        file.seekg(0, ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, ios::beg);

        vector<char> compressed_data(file_size);
        file.read(compressed_data.data(), file_size);

        size_t uncompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());
        if (uncompressed_size == ZSTD_CONTENTSIZE_ERROR) {
            cerr << "Error: No se pudo determinar su tamaño descomprimido" << endl;
            return false;
        } else if (uncompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            cerr << "Error: Peso de archivo original desconocido" << endl;
            return false;
        }

        vector<char> uncompressed_data(uncompressed_size);
        size_t actual_uncompressed_size = ZSTD_decompress(uncompressed_data.data(), uncompressed_size, compressed_data.data(), compressed_data.size());
        if (ZSTD_isError(actual_uncompressed_size)) {
            cerr << "Error de descompresion: " << ZSTD_getErrorName(actual_uncompressed_size) << endl;
            return false;
        }

        istringstream buffer(string(uncompressed_data.data(), actual_uncompressed_size));
        root = new BTreeNode(t, true);
        root->deserialize(buffer);
        deserialize_string_pool(buffer);

        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
        cout << "B-Tree deserializado en " << duration.count() << " milisegundos." << endl;

        file.close();
        return true;
    } else {
        cerr << "Error abriendo archivo para deserializacion." << endl;
        return false;
    }
}

// Función para eliminar caracteres de control no deseados como \r
std::string clean_string(const std::string& input) {
    std::string output;
    std::remove_copy_if(input.begin(), input.end(), std::back_inserter(output), [](char c) {
        return c == '\r';  // Puedes agregar más caracteres no deseados aquí si es necesario
    });
    return output;
}

// Función para escapar caracteres en una cadena para JSON
std::string escape_json(const std::string& input) {
    std::string cleaned = clean_string(input);
    std::string output;
    output.reserve(cleaned.length());
    for (char c : cleaned) {
        switch (c) {
        case '"':  output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if ('\x00' <= c && c <= '\x1f') {
                output += "\\u" + std::to_string(c);
            } else {
                output += c;
            }
        }
    }
    return output;
}

class BTreeManager {
public:
    static bool loadFile(const string& input_filename, Btree& tree) {
        ifstream input_file(input_filename, ios::binary);
        if (!input_file) {
            cerr << "Error: No se pudo abrir el archivo" << endl;
            return false;
        }

        vector<char> compressed_data((istreambuf_iterator<char>(input_file)), istreambuf_iterator<char>());
        input_file.close();

        size_t uncompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());
        if (uncompressed_size == ZSTD_CONTENTSIZE_ERROR) {
            cerr << "Error: No se pudo determinar su tamaño descomprimido" << endl;
            return false;
        } else if (uncompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            cerr << "Error: Peso de archivo original desconocido" << endl;
            return false;
        }

        vector<char> uncompressed_data(uncompressed_size);
        size_t actual_uncompressed_size = ZSTD_decompress(uncompressed_data.data(), uncompressed_size, compressed_data.data(), compressed_data.size());
        if (ZSTD_isError(actual_uncompressed_size)) {
            cerr << "Error de descompresion: " << ZSTD_getErrorName(actual_uncompressed_size) << endl;
            return false;
        }

        const char* file_data = uncompressed_data.data();
        const char* file_end = file_data + actual_uncompressed_size;

        auto start_parse = chrono::high_resolution_clock::now();

        while (file_data < file_end) {
            const char* line_end = std::find(file_data, file_end, '\n');
            string line(file_data, line_end);

            vector<string> fields;
            istringstream ss(line);
            string field;
            while (getline(ss, field, ',')) {
                fields.push_back(field);
            }

            if (fields.size() == 10) {
                string dni = fields[0];
                string nombres = fields[1];
                string apellidos = fields[2];
                string lugar_nacimiento = fields[3];
                Direccion direccion = { tree.get_pool_index(fields[4]), tree.get_pool_index(fields[5]), tree.get_pool_index(fields[6]), tree.get_pool_index(fields[7]), tree.get_pool_index(fields[8]) };
                string correo = fields[9];

                tree.insert(new Ciudadano(dni.c_str(), tree.get_pool_index(nombres), tree.get_pool_index(apellidos), tree.get_pool_index(lugar_nacimiento), direccion, 987654321, tree.get_pool_index(correo), "PE", 0, 0));
            }

            file_data = line_end + 1;
        }

        auto stop_parse = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(stop_parse - start_parse);
        cout << "Tiempo de lectura: " << duration.count() / 1000.0 << "s\n";
        return true;
    }

    static string searchDNI(const Btree& tree, const string& dniToSearch) {
        const Ciudadano* found = tree.search(dniToSearch);
        std::string jsonResult = "{";

        if (found) {
            jsonResult += "\"DNI\": \"" + escape_json(found->getDni()) + "\",";
            jsonResult += "\"Nombres\": \"" + escape_json(tree.get_string_from_pool(found->getNombres())) + "\",";
            jsonResult += "\"Apellidos\": \"" + escape_json(tree.get_string_from_pool(found->getApellidos())) + "\",";
            jsonResult += "\"Lugar de Nacimiento\": \"" + escape_json(tree.get_string_from_pool(found->getLugarNacimiento())) + "\",";
            
            Direccion dir = found->getDireccion();
            jsonResult += "\"Direccion\": {";
            jsonResult += "\"Departamento\": \"" + escape_json(tree.get_string_from_pool(dir.departamento)) + "\",";
            jsonResult += "\"Provincia\": \"" + escape_json(tree.get_string_from_pool(dir.provincia)) + "\",";
            jsonResult += "\"Ciudad\": \"" + escape_json(tree.get_string_from_pool(dir.ciudad)) + "\",";
            jsonResult += "\"Distrito\": \"" + escape_json(tree.get_string_from_pool(dir.distrito)) + "\",";
            jsonResult += "\"Ubicacion\": \"" + escape_json(tree.get_string_from_pool(dir.ubicacion)) + "\"";
            jsonResult += "},"; // Cierra el objeto Dirección
            
            jsonResult += "\"Telefono\": \"" + escape_json(std::to_string(found->getTelefono())) + "\",";
            jsonResult += "\"Correo\": \"" + escape_json(tree.get_string_from_pool(found->getCorreo())) + "\",";
            jsonResult += "\"Nacionalidad\": \"" + escape_json(found->getNacionalidad()) + "\",";
            jsonResult += "\"Sexo\": \"" + escape_json(found->getSexo() == 0 ? "Masculino" : "Femenino") + "\",";
            jsonResult += "\"Estado Civil\": \"" + escape_json(found->getEstadoCivil() == 0 ? "Soltero" : "Casado") + "\"";
        } else {
            jsonResult += "\"error\": \"DNI " + escape_json(dniToSearch) + " no encontrado.\"";
        }

        jsonResult += "}";
        return jsonResult;
    }
};

Btree tree(30);

class MyHandler : public Http::Handler {
    HTTP_PROTOTYPE(MyHandler)

    void onRequest(const Http::Request& req, Http::ResponseWriter response) override {
        if (req.resource() == "/create") {
            if (req.method() == Http::Method::Post) {
                try {
                    auto body = req.body();
                    string path = body; // Leer el path directamente del cuerpo de la solicitud
                    bool result = BTreeManager::loadFile(path, tree);
                    if (result) {
                        response.send(Http::Code::Ok, R"({"result": "Data descomprimida e insertada"})", MIME(Application, Json));
                    } else {
                        response.send(Http::Code::Internal_Server_Error, R"({"error": "Error al cargar el archivo"})", MIME(Application, Json));
                    }
                } catch (const std::exception& e) {
                    response.send(Http::Code::Internal_Server_Error, R"({"error": "Excepción: )" + std::string(e.what()) + R"("})", MIME(Application, Json));
                }
            }
        } else  if (req.resource() == "/save") {
            if (req.method() == Http::Method::Post) {
                try {
                    auto body = req.body();
                    string path = body; // Leer el path directamente del cuerpo de la solicitud

                    bool result = tree.serialize(path);
                    if (result) {
                        response.send(Http::Code::Ok, R"({"result": "Datos guardados en archivo"})", MIME(Application, Json));
                    } else {
                        response.send(Http::Code::Internal_Server_Error, R"({"error": "Error al guardar el archivo"})", MIME(Application, Json));
                    }
                } catch (const std::exception& e) {
                    response.send(Http::Code::Internal_Server_Error, R"({"error": "Excepción: )" + std::string(e.what()) + R"("})", MIME(Application, Json));
                }
            }
        }
        else if (req.resource() == "/open") {
            if (req.method() == Http::Method::Post) {
                try {
                    auto body = req.body();
                    string path = body; // Leer el path directamente del cuerpo de la solicitud
                    bool result = tree.deserialize(path);
                    if (result) {
                        response.send(Http::Code::Ok, R"({"result": "Datos importados correctamente"})", MIME(Application, Json));
                    } else {
                        response.send(Http::Code::Internal_Server_Error, R"({"error": "Error en la importacion"})", MIME(Application, Json));
                    }
                } catch (const std::exception& e) {
                    response.send(Http::Code::Internal_Server_Error, R"({"error": "Excepción: )" + std::string(e.what()) + R"("})", MIME(Application, Json));
                }
            }
        } else if (req.resource() == "/search") {
            if (req.method() == Http::Method::Get) {
                try {
                    string dniSearch;
                    const auto& query = req.query();
                    if (query.get("dni").has_value()) {
                        dniSearch = query.get("dni").value();
                    }
                    string result = BTreeManager::searchDNI(tree, dniSearch);
                    response.send(Http::Code::Ok, result, MIME(Application, Json));
                } catch (const std::exception& e) {
                    response.send(Http::Code::Internal_Server_Error, R"({"error": "Excepción: )" + std::string(e.what()) + R"("})", MIME(Application, Json));
                }
            }
        } else if (req.resource() == "/delete") {
            if (req.method() == Http::Method::Get) {
                try {
                    string dniToDelete;
                    const auto& query = req.query();
                    if (query.get("dni").has_value()) {
                        dniToDelete = query.get("dni").value();
                    }
                    tree.remove(dniToDelete);
                    response.send(Http::Code::Ok, R"({"result": "DNI eliminado correctamente"})", MIME(Application, Json));
                } catch (const std::exception& e) {
                    response.send(Http::Code::Internal_Server_Error, R"({"error": "Excepción: )" + std::string(e.what()) + R"("})", MIME(Application, Json));
                }
            }
        } else if (req.resource() == "/add") {
            if (req.method() == Http::Method::Post) {
                try {
                    auto body = req.body();
                    istringstream ss(body);
                    string field;
                    vector<string> fields;

                    while (getline(ss, field, ',')) {
                        fields.push_back(field);
                    }

                    if (fields.size() == 14) {
                        string dni = fields[0];
                        uint32_t nombres = tree.get_pool_index(fields[1]);
                        uint32_t apellidos = tree.get_pool_index(fields[2]);
                        uint32_t lugar_nacimiento = tree.get_pool_index(fields[3]);
                        Direccion direccion = { tree.get_pool_index(fields[4]), tree.get_pool_index(fields[5]), tree.get_pool_index(fields[6]), tree.get_pool_index(fields[7]), tree.get_pool_index(fields[8]) };
                        uint64_t telefono = stoull(fields[9]);
                        uint32_t correo = tree.get_pool_index(fields[10]);
                        string nacionalidad = fields[11];
                        unsigned sexo = static_cast<unsigned>(stoi(fields[12]));
                        unsigned estado_civil = static_cast<unsigned>(stoi(fields[13]));

                        Ciudadano* newCitizen = new Ciudadano(dni.c_str(), nombres, apellidos, lugar_nacimiento, direccion, telefono, correo, nacionalidad.c_str(), sexo, estado_civil);
                        tree.insert(newCitizen);

                        response.send(Http::Code::Ok, R"({"result": "Ciudadano agregado correctamente"})", MIME(Application, Json));
                    } else {
                        response.send(Http::Code::Bad_Request, R"({"error": "Formato de entrada incorrecto"})", MIME(Application, Json));
                    }
                } catch (const std::exception& e) {
                    response.send(Http::Code::Internal_Server_Error, R"({"error": "Excepción: )" + std::string(e.what()) + R"("})", MIME(Application, Json));
                }
            }
        }
        else {
            response.send(Http::Code::Not_Found);
        }
    }

    void onTimeout(const Http::Request& /*req*/, Http::ResponseWriter response) override {
        response
            .send(Http::Code::Request_Timeout, "Timeout")
            .then([=](ssize_t) {}, PrintException());
    }
};

int main(int argc, char* argv[]) {
    Port port(5002);

    int thr = 40;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    std::cout << "Cores = " << hardware_concurrency() << std::endl;
    std::cout << "Using " << thr << " threads" << std::endl;

    auto server = std::make_shared<Http::Endpoint>(addr);

    auto opts = Http::Endpoint::options()
                    .threads(thr);
    server->init(opts);
    server->setHandler(Http::make_handler<MyHandler>());
    server->serve();
}
