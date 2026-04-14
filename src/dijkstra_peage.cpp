#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstring>

// ===================== Config =====================
static const char*  CSV_PATH = "data/tarifs_area.csv"; // CSV with distance_km
static const int    DEFAULT_TOP_K = 5;            // (old) Yen K options
static const double PROGRESS_EPS_KM = 0.01;       // marge "rem[v] < rem[u]"
static const bool   ENABLE_PROGRESS_FILTER = true;// Option C
static const int    MAX_SEGMENTS_TO_SHOW = 12;    // options nb segments (1..N)
// ==================================================

static std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (unsigned char)s[a] <= 32) ++a;
    size_t b = s.size();
    while (b > a && (unsigned char)s[b-1] <= 32) --b;
    return s.substr(a, b-a);
}

// --- ASCII fold FR ("gnu-friendly") ---
static std::string ascii_fold_fr(std::string s) {
    auto rep = [&](const std::string& from, const char* to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += std::strlen(to);
        }
    };

    rep(u8"à","a"); rep(u8"â","a"); rep(u8"ä","a"); rep(u8"á","a"); rep(u8"ã","a");
    rep(u8"À","A"); rep(u8"Â","A"); rep(u8"Ä","A"); rep(u8"Á","A"); rep(u8"Ã","A");

    rep(u8"é","e"); rep(u8"è","e"); rep(u8"ê","e"); rep(u8"ë","e");
    rep(u8"É","E"); rep(u8"È","E"); rep(u8"Ê","E"); rep(u8"Ë","E");

    rep(u8"î","i"); rep(u8"ï","i"); rep(u8"ì","i"); rep(u8"í","i");
    rep(u8"Î","I"); rep(u8"Ï","I"); rep(u8"Ì","I"); rep(u8"Í","I");

    rep(u8"ô","o"); rep(u8"ö","o"); rep(u8"ò","o"); rep(u8"ó","o"); rep(u8"õ","o");
    rep(u8"Ô","O"); rep(u8"Ö","O"); rep(u8"Ò","O"); rep(u8"Ó","O"); rep(u8"Õ","O");

    rep(u8"ù","u"); rep(u8"û","u"); rep(u8"ü","u"); rep(u8"ú","u");
    rep(u8"Ù","U"); rep(u8"Û","U"); rep(u8"Ü","U"); rep(u8"Ú","U");

    rep(u8"ÿ","y"); rep(u8"Ÿ","Y");

    rep(u8"ç","c"); rep(u8"Ç","C");

    rep(u8"œ","oe"); rep(u8"Œ","OE");
    rep(u8"æ","ae"); rep(u8"Æ","AE");

    rep(u8"’","'");
    rep(u8"–","-"); rep(u8"—","-");

    return s;
}

struct Edge {
    int to;
    double dist_km;
    double cost_by_class[5];
};

struct NodeInfo {
    std::string code;
    std::string name;
};

struct Graph {
    std::vector<std::vector<Edge>> adj;
    std::vector<NodeInfo> nodes;
    std::unordered_map<std::string,int> code_to_id;

    int get_or_add(const std::string& code, const std::string& name) {
        auto it = code_to_id.find(code);
        if (it != code_to_id.end()) {
            int id = it->second;
            if (!name.empty() && nodes[id].name.empty()) nodes[id].name = name;
            return id;
        }
        int id = (int)adj.size();
        code_to_id.emplace(code, id);
        nodes.push_back({code, name});
        adj.emplace_back();
        return id;
    }

    void add_edge(const std::string& from_code, const std::string& from_name,
                  const std::string& to_code,   const std::string& to_name,
                  double dist_km,
                  double c1, double c2, double c3, double c4, double c5) {
        int u = get_or_add(from_code, from_name);
        int v = get_or_add(to_code, to_name);
        Edge e{};
        e.to = v;
        e.dist_km = dist_km;
        e.cost_by_class[0] = c1;
        e.cost_by_class[1] = c2;
        e.cost_by_class[2] = c3;
        e.cost_by_class[3] = c4;
        e.cost_by_class[4] = c5;
        adj[u].push_back(e);
    }

    bool best_edge(int u, int v, int clsIdx, Edge& out) const {
        bool found = false;
        double best = std::numeric_limits<double>::infinity();
        for (const auto& e : adj[u]) {
            if (e.to == v) {
                double c = e.cost_by_class[clsIdx];
                if (c < best) { best = c; out = e; found = true; }
            }
        }
        return found;
    }

    bool direct_cost_distance(int u, int v, int clsIdx, double& cost, double& dist) const {
        Edge e{};
        if (!best_edge(u, v, clsIdx, e)) return false;
        cost = e.cost_by_class[clsIdx];
        dist = e.dist_km;
        return true;
    }
};

// ---------------- CSV parsing ----------------
static std::vector<std::string> split_csv_simple_semicolon(const std::string& line) {
    std::vector<std::string> cols;
    cols.reserve(16);
    std::string cur;
    for (char c : line) {
        if (c == ';') { cols.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    cols.push_back(cur);
    return cols;
}

static bool to_double(const std::string& s, double& out) {
    try { out = std::stod(s); return true; }
    catch (...) { return false; }
}

static Graph load_graph_csv(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Impossible d'ouvrir le CSV: " + path);

    std::string header;
    if (!std::getline(f, header)) throw std::runtime_error("CSV vide");

    auto h = split_csv_simple_semicolon(header);
    if (h.size() < 10 ||
        trim(h[0]) != "code_entree" ||
        trim(h[1]) != "gare_entree" ||
        trim(h[2]) != "code_sortie" ||
        trim(h[3]) != "gare_sortie" ||
        trim(h[4]) != "distance_km") {
        throw std::runtime_error(
            "Header CSV inattendu (attendu: code_entree;gare_entree;code_sortie;gare_sortie;distance_km;tarif_classe_1..5)"
        );
    }

    Graph g;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = split_csv_simple_semicolon(line);
        if (c.size() < 10) continue;

        std::string codeE = trim(c[0]);
        std::string gareE = trim(c[1]);
        std::string codeS = trim(c[2]);
        std::string gareS = trim(c[3]);

        double dist_km = 0.0;
        if (!to_double(trim(c[4]), dist_km)) continue;

        double c1=0, c2=0, c3=0, c4=0, c5=0;
        if (!to_double(trim(c[5]), c1)) continue;
        if (!to_double(trim(c[6]), c2)) continue;
        if (!to_double(trim(c[7]), c3)) continue;
        if (!to_double(trim(c[8]), c4)) continue;
        if (!to_double(trim(c[9]), c5)) continue;

        if (dist_km < 0) continue;
        if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0 || c5 < 0) continue;

        g.add_edge(codeE, gareE, codeS, gareS, dist_km, c1, c2, c3, c4, c5);
    }
    return g;
}

// ==========================================================
// check "rem[u]" = minimal distance from u to dst
// ==========================================================
static std::vector<double> compute_remaining_distance_to_dst(const Graph& g, int dst) {
    const double INF = std::numeric_limits<double>::infinity();
    int n = (int)g.adj.size();

    std::vector<std::vector<std::pair<int,double>>> inv(n);
    for (int u = 0; u < n; ++u) {
        for (const auto& e : g.adj[u]) {
            inv[e.to].push_back({u, e.dist_km});
        }
    }

    std::vector<double> rem(n, INF);
    using PQ = std::pair<double,int>;
    std::priority_queue<PQ, std::vector<PQ>, std::greater<>> pq;

    rem[dst] = 0.0;
    pq.push({0.0, dst});

    while (!pq.empty()) {
        auto [d,v] = pq.top(); pq.pop();
        if (d != rem[v]) continue;

        for (auto [u, w] : inv[v]) {
            double nd = d + w;
            if (nd < rem[u]) {
                rem[u] = nd;
                pq.push({nd, u});
            }
        }
    }
    return rem;
}

// --------------------- Paths ---------------------
struct Path {
    double total_cost = std::numeric_limits<double>::infinity();
    double total_dist_km = 0.0;
    std::vector<int> nodes;
    int segments = 0; // nb segments = nodes.size()-1
};

static double path_cost(const Graph& g, const std::vector<int>& nodes, int clsIdx) {
    double sum = 0.0;
    for (size_t i = 0; i + 1 < nodes.size(); ++i) {
        int u = nodes[i], v = nodes[i+1];
        Edge e{};
        if (!g.best_edge(u, v, clsIdx, e)) return std::numeric_limits<double>::infinity();
        sum += e.cost_by_class[clsIdx];
    }
    return sum;
}

static double path_distance(const Graph& g, const std::vector<int>& nodes, int clsIdx) {
    double sum = 0.0;
    for (size_t i = 0; i + 1 < nodes.size(); ++i) {
        int u = nodes[i], v = nodes[i+1];
        Edge e{};
        if (!g.best_edge(u, v, clsIdx, e)) return std::numeric_limits<double>::infinity();
        sum += e.dist_km;
    }
    return sum;
}

// ==========================================================
// Dijkstra "multi-segments": best path by nb segments
// state = (node, used_segments)
// ==========================================================
struct ParentState {
    int prev_node = -1;
    int prev_k = -1;
};

static std::vector<Path> best_paths_by_segments(const Graph& g, int src, int dst, int clsIdx,
                                                int maxK,
                                                const std::vector<double>* rem_to_dst /*Option C*/) {
    const double INF = std::numeric_limits<double>::infinity();
    int n = (int)g.adj.size();

    // dist[k][v]
    std::vector<std::vector<double>> dist(maxK + 1, std::vector<double>(n, INF));
    std::vector<std::vector<ParentState>> parent(maxK + 1, std::vector<ParentState>(n));

    struct QItem {
        double cost;
        int k;
        int v;
        bool operator>(const QItem& o) const { return cost > o.cost; }
    };

    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;
    dist[0][src] = 0.0;
    pq.push({0.0, 0, src});

    while (!pq.empty()) {
        auto cur = pq.top(); pq.pop();
        if (cur.cost != dist[cur.k][cur.v]) continue;

        if (cur.k == maxK) continue;

        for (const auto& e : g.adj[cur.v]) {
            int to = e.to;
            int nk = cur.k + 1;

            // progress to dst
            if (ENABLE_PROGRESS_FILTER && rem_to_dst) {
                double ru = (*rem_to_dst)[cur.v];
                double rv = (*rem_to_dst)[to];
                if (!std::isfinite(ru) || !std::isfinite(rv)) continue;
                if (!(rv < ru - PROGRESS_EPS_KM)) continue;
            }

            double w = e.cost_by_class[clsIdx];
            double nd = cur.cost + w;

            if (nd < dist[nk][to]) {
                dist[nk][to] = nd;
                parent[nk][to] = {cur.v, cur.k};
                pq.push({nd, nk, to});
            }
        }
    }

    // construct paths
    std::vector<Path> out;
    out.reserve(maxK);

    for (int k = 1; k <= maxK; ++k) {
        if (!std::isfinite(dist[k][dst])) continue;

        std::vector<int> nodes;
        nodes.reserve(k + 1);

        int v = dst;
        int ck = k;
        while (!(v == src && ck == 0)) {
            nodes.push_back(v);
            ParentState ps = parent[ck][v];
            if (ps.prev_node < 0) break; 
            v = ps.prev_node;
            ck = ps.prev_k;
        }
        nodes.push_back(src);
        std::reverse(nodes.begin(), nodes.end());

        Path p;
        p.nodes = std::move(nodes);
        p.segments = (int)p.nodes.size() - 1;
        p.total_cost = dist[k][dst];
        p.total_dist_km = path_distance(g, p.nodes, clsIdx);
        out.push_back(std::move(p));
    }

    // trie par nb segments croissant
    std::sort(out.begin(), out.end(), [](const Path& a, const Path& b){
        return a.segments < b.segments;
    });

    return out;
}

// ==========================================================
// Yen K-shortest:
// ==========================================================
/*
struct Ban {
    std::set<std::pair<int,int>> banned_edges;
    std::set<int> banned_nodes;
};

static Path dijkstra_banned(const Graph& g, int src, int dst, int clsIdx,
                           const Ban& ban,
                           const std::vector<double>* rem_to_dst) { ... }

static std::string path_key(const std::vector<int>& p) { ... }

static std::vector<Path> k_shortest_paths_yen(const Graph& g, int src, int dst, int clsIdx, int K,
                                              const std::vector<double>* rem_to_dst) { ... }
*/

// --------------------- JSON export---------------------
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size()+8);
    for (char c : s) {
        switch(c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)(unsigned char)c;
                    out += oss.str();
                } else out += c;
        }
    }
    return out;
}

bool write_utf8_file(const std::wstring& path, const std::string& content) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;

    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    return true;
}

static std::wstring get_exe_dir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return p.substr(0, pos);
}

static std::string node_label_ascii(const Graph& g, int id) {
    const auto& n = g.nodes[id];
    return ascii_fold_fr(n.name) + " [" + n.code + "]";
}

// ===== Gain exit / no exit (local) =====
struct ExitDecision {
    bool direct_exists = false;

    double direct_cost = 0.0;
    double direct_dist_km = 0.0;

    double cost_with_exit = 0.0;      // prev->mid + mid->next
    double dist_with_exit_km = 0.0;

    double gain = 0.0;               // direct - with_exit
    bool exit_necessary = false;      // no direct
    bool exit_beneficial = false;     // gain > 0
};

static ExitDecision compute_exit_decision(const Graph& g, int prev, int mid, int next, int clsIdx) {
    ExitDecision d{};

    Edge e1{}, e2{};
    if (!g.best_edge(prev, mid, clsIdx, e1)) return d;
    if (!g.best_edge(mid, next, clsIdx, e2)) return d;

    d.cost_with_exit = e1.cost_by_class[clsIdx] + e2.cost_by_class[clsIdx];
    d.dist_with_exit_km = e1.dist_km + e2.dist_km;

    d.direct_exists = g.direct_cost_distance(prev, next, clsIdx, d.direct_cost, d.direct_dist_km);

    if (!d.direct_exists) {
        d.exit_necessary = true;
        d.exit_beneficial = true;
        d.gain = 0.0;
    } else {
        d.gain = d.direct_cost - d.cost_with_exit;
        d.exit_beneficial = (d.gain > 0.0001);
        d.exit_necessary = false;
    }
    return d;
}

// ===================== Win32 GUI =====================
static const int IDC_FROM   = 1001;
static const int IDC_TO     = 1002;
static const int IDC_CLASS  = 1006;
static const int IDC_CALC   = 1003;
static const int IDC_EXPORT = 1005;
static const int IDC_OUTPUT = 1004;

static Graph g_graph;
static bool  g_loaded = false;

// last result
static int g_last_srcId = -1;
static int g_last_dstId = -1;
static int g_last_class = 1;
static bool g_last_has_direct = false;
static double g_last_direct_cost = 0.0;
static double g_last_direct_dist = 0.0;

// option by nb segments
static std::vector<Path> g_last_by_segments;

static std::wstring to_wstring_utf8(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) {
        len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), w.data(), len);
        return w;
    }
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}

static void set_output(HWND hWnd, const std::wstring& txt) {
    SetWindowTextW(GetDlgItem(hWnd, IDC_OUTPUT), txt.c_str());
}

static void combo_fill_stations(HWND hCombo, const Graph& g) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);

    // 1) Construire une liste (label_affiche, node_id)
    std::vector<std::pair<std::string, int>> items;
    items.reserve(g.nodes.size());

    for (int id = 0; id < (int)g.nodes.size(); ++id) {
        std::string label;
        if (!g.nodes[id].name.empty())
            label = ascii_fold_fr(g.nodes[id].name);
        else
            label = g.nodes[id].code;

        items.emplace_back(label, id);
    }

    // 2) Tri alphabétique (case-insensitive ASCII)
    std::sort(items.begin(), items.end(),
        [](const auto& a, const auto& b) {
            return _stricmp(a.first.c_str(), b.first.c_str()) < 0;
        }
    );

    // 3) Remplissage de la combo dans l’ordre trié
    for (const auto& it : items) {
        std::wstring wName = to_wstring_utf8(it.first);
        LRESULT idx = SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)wName.c_str());
        if (idx != CB_ERR && idx != CB_ERRSPACE) {
            SendMessageW(hCombo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)it.second);
        }
    }

    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}


static void combo_fill_classes(HWND hCombo) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    for (int c = 1; c <= 5; ++c) {
        std::wstring w = L"Classe " + std::to_wstring(c);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)w.c_str());
    }
    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

static int combo_get_selected_node_id(HWND hCombo) {
    int idx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) return -1;
    return (int)SendMessageW(hCombo, CB_GETITEMDATA, idx, 0);
}

static int get_selected_class(HWND hCombo) {
    int idx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) return 1;
    return idx + 1;
}

// ===== options by nb segments =====
static std::wstring format_options_verbose(const Graph& g,
                                          int srcId, int dstId,
                                          int vehicle_class,
                                          bool has_direct,
                                          double direct_cost,
                                          double direct_dist_km,
                                          const std::vector<Path>& by_segments) {
    std::wstringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);

    ss << L"========== Optimisation Peage ==========\r\n";
    ss << L"Categorie vehicule : Classe " << vehicle_class << L"\r\n";
    ss << L"Origine : " << to_wstring_utf8(node_label_ascii(g, srcId)) << L"\r\n";
    ss << L"Destination : " << to_wstring_utf8(node_label_ascii(g, dstId)) << L"\r\n";
    ss << L"Options par nombre de sorties/segments (1.." << MAX_SEGMENTS_TO_SHOW << L")\r\n";
    ss << L"Filtre progression vers destination (Option C) : " << (ENABLE_PROGRESS_FILTER ? L"ON" : L"OFF") << L"\r\n\r\n";

    if (has_direct) {
        ss << L"[Option DIRECTE (1 transaction)]\r\n";
        ss << L"  A -> C : " << direct_cost << L" EUR | distance: " << direct_dist_km << L" km\r\n\r\n";
    } else {
        ss << L"[Option DIRECTE]\r\n";
        ss << L"  Aucun tarif direct A -> C dans le CSV.\r\n\r\n";
    }

    if (by_segments.empty()) {
        ss << L"Aucune option trouvee (graphe deconnecte / filtre trop strict / destination non atteignable).\r\n";
        return ss.str();
    }

    // best global from options k<=N
    const Path* best = nullptr;
    for (const auto& p : by_segments) {
        if (!best || p.total_cost < best->total_cost) best = &p;
    }

    ss << L"========== MEILLEUR TARIF (global) ==========\r\n";
    ss << L"Meilleur cout total : " << best->total_cost << L" EUR\r\n";
    ss << L"Distance totale (somme segments) : " << best->total_dist_km << L" km\r\n";
    ss << L"Nombre de transactions (segments) : " << best->segments << L"\r\n";

    if (has_direct) {
        double diff = direct_cost - best->total_cost;
        ss << L"Comparaison vs direct : direct - meilleur = " << diff << L" EUR\r\n";
        if (diff > 0.0001) ss << L"OK: Decomposition (sortir/rentrer) moins chere que le direct.\r\n";
        else if (diff < -0.0001) ss << L"Info: Le direct est moins cher.\r\n";
        else ss << L"Info: Egalite (a l'arrondi pres).\r\n";
    }
    ss << L"\r\n";

    // 
    ss << L"========== OPTIONS PAR NOMBRE DE SEGMENTS ==========\r\n";
    ss << L"(segments = transactions, sorties intermediaires = segments-1)\r\n\r\n";

    for (int k = 1; k <= MAX_SEGMENTS_TO_SHOW; ++k) {
        const Path* pk = nullptr;
        for (const auto& p : by_segments) if (p.segments == k) { pk = &p; break; }

        ss << L"- Segments " << k << L" (sorties intermediaires: " << (k-1) << L") : ";
        if (!pk) {
            ss << L"IMPOSSIBLE\r\n";
            continue;
        }

        ss << pk->total_cost << L" EUR | " << pk->total_dist_km << L" km\r\n";
        ss << L"  Chemin: ";
        for (size_t i = 0; i < pk->nodes.size(); ++i) {
            if (i) ss << L"->";
            ss << to_wstring_utf8(ascii_fold_fr(g.nodes[pk->nodes[i]].name));
        }
        ss << L"\r\n";
    }
    ss << L"\r\n";

    // detail for best path
    ss << L"[Detail du meilleur chemin]\r\n";
    double cumul_cost = 0.0;
    double cumul_km = 0.0;
    int clsIdx = vehicle_class - 1;

    for (size_t i = 0; i + 1 < best->nodes.size(); ++i) {
        int u = best->nodes[i];
        int v = best->nodes[i+1];

        Edge e{};
        double seg_cost = 0.0, seg_km = 0.0;
        if (g.best_edge(u, v, clsIdx, e)) {
            seg_cost = e.cost_by_class[clsIdx];
            seg_km = e.dist_km;
        }

        cumul_cost += seg_cost;
        cumul_km += seg_km;

        ss << L"  Segment " << (i+1) << L":\r\n";
        ss << L"    Entree : " << to_wstring_utf8(node_label_ascii(g, u)) << L"\r\n";
        ss << L"    Sortie : " << to_wstring_utf8(node_label_ascii(g, v)) << L"\r\n";
        ss << L"    Cout segment : " << seg_cost << L" EUR\r\n";
        ss << L"    Distance segment : " << seg_km << L" km\r\n";
        ss << L"    Cumul : " << cumul_cost << L" EUR | " << cumul_km << L" km\r\n";

        // 
        if (i + 2 < best->nodes.size()) {
            int next = best->nodes[i+2];
            ExitDecision dec = compute_exit_decision(g, u, v, next, clsIdx);

            ss << L"    ---- Sortie a la gare intermediaire ? ----\r\n";
            ss << L"    Gare intermediaire : " << to_wstring_utf8(node_label_ascii(g, v)) << L"\r\n";

            ss << L"    Option AVEC sortie : "
               << dec.cost_with_exit << L" EUR | " << dec.dist_with_exit_km << L" km ("
               << to_wstring_utf8(node_label_ascii(g, u)) << L" -> "
               << to_wstring_utf8(node_label_ascii(g, v)) << L" -> "
               << to_wstring_utf8(node_label_ascii(g, next)) << L")\r\n";

            if (!dec.direct_exists) {
                ss << L"    Option SANS sortie : IMPOSSIBLE (pas de tarif direct "
                   << to_wstring_utf8(node_label_ascii(g, u)) << L" -> "
                   << to_wstring_utf8(node_label_ascii(g, next)) << L")\r\n";
                ss << L"    => Sortie NECESSAIRE pour pouvoir decomposer.\r\n";
            } else {
                ss << L"    Option SANS sortie : "
                   << dec.direct_cost << L" EUR | " << dec.direct_dist_km << L" km ("
                   << to_wstring_utf8(node_label_ascii(g, u)) << L" -> "
                   << to_wstring_utf8(node_label_ascii(g, next)) << L")\r\n";

                if (dec.exit_beneficial) {
                    ss << L"    => Sortie BENEFIQUE. Gain = " << dec.gain << L" EUR\r\n";
                } else if (std::abs(dec.gain) <= 0.0001) {
                    ss << L"    => Sortie NEUTRE (egalite a l'arrondi pres).\r\n";
                } else {
                    ss << L"    => Sortie NON rentable. Surcout = " << (-dec.gain) << L" EUR\r\n";
                }
            }
        }

        ss << L"\r\n";
    }

    return ss.str();
}

// ===================== App state & handlers =====================
static void show_info(HWND hWnd, const wchar_t* title, const wchar_t* msg) {
    MessageBoxW(hWnd, msg, title, MB_OK | MB_ICONINFORMATION);
}
static void show_error(HWND hWnd, const wchar_t* title, const wchar_t* msg) {
    MessageBoxW(hWnd, msg, title, MB_OK | MB_ICONERROR);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Gare d'entree:", WS_CHILD | WS_VISIBLE, 15, 15, 120, 20, hWnd, nullptr, nullptr, nullptr);
        HWND hFrom = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   140, 12, 300, 300, hWnd, (HMENU)IDC_FROM, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Gare de sortie:", WS_CHILD | WS_VISIBLE, 15, 50, 120, 20, hWnd, nullptr, nullptr, nullptr);
        HWND hTo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 140, 47, 300, 300, hWnd, (HMENU)IDC_TO, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Categorie:", WS_CHILD | WS_VISIBLE, 460, 15, 70, 20, hWnd, nullptr, nullptr, nullptr);
        HWND hClass = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                    535, 12, 120, 200, hWnd, (HMENU)IDC_CLASS, nullptr, nullptr);
        combo_fill_classes(hClass);

        CreateWindowW(L"BUTTON", L"Calculer (Options par nb sorties)", WS_CHILD | WS_VISIBLE, 460, 47, 195, 27, hWnd, (HMENU)IDC_CALC, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Exporter JSON", WS_CHILD | WS_VISIBLE, 460, 78, 195, 27, hWnd, (HMENU)IDC_EXPORT, nullptr, nullptr);

        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
                      15, 115, 640, 360, hWnd, (HMENU)IDC_OUTPUT, nullptr, nullptr);

        if (!g_loaded) {
            try {
                g_graph = load_graph_csv(CSV_PATH);
                g_loaded = true;

                combo_fill_stations(hFrom, g_graph);
                combo_fill_stations(hTo, g_graph);

                std::wstringstream ss;
                ss << L"CSV charge: " << to_wstring_utf8(CSV_PATH) << L"\r\n"
                   << L"- CSV contient distance_km\r\n"
                   << L"- Option C (progression vers destination) : " << (ENABLE_PROGRESS_FILTER ? L"ON" : L"OFF") << L"\r\n"
                   << L"- Options affichees par nb segments: 1.." << MAX_SEGMENTS_TO_SHOW << L"\r\n"
                   << L"- Listes deroulantes: noms ASCII (sans code)\r\n";
                set_output(hWnd, ss.str());
            } catch (const std::exception& e) {
                std::wstring w = L"Erreur chargement CSV.\r\n";
                w += to_wstring_utf8(e.what());
                set_output(hWnd, w);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_CALC && code == BN_CLICKED) {
            int srcId = combo_get_selected_node_id(GetDlgItem(hWnd, IDC_FROM));
            int dstId = combo_get_selected_node_id(GetDlgItem(hWnd, IDC_TO));
            if (srcId < 0 || dstId < 0) {
                set_output(hWnd, L"Choisis une gare d'entree et une gare de sortie.");
                return 0;
            }

            g_last_srcId = srcId;
            g_last_dstId = dstId;
            g_last_class = get_selected_class(GetDlgItem(hWnd, IDC_CLASS));

            int clsIdx = g_last_class - 1;

            // precompute remaining distance to dst
            std::vector<double> rem_to_dst = compute_remaining_distance_to_dst(g_graph, dstId);

            // Direct A->C (k=1)
            g_last_has_direct = g_graph.direct_cost_distance(srcId, dstId, clsIdx, g_last_direct_cost, g_last_direct_dist);

            // options  by nb segments
            g_last_by_segments = best_paths_by_segments(
                g_graph, srcId, dstId, clsIdx,
                MAX_SEGMENTS_TO_SHOW,
                ENABLE_PROGRESS_FILTER ? &rem_to_dst : nullptr
            );

            set_output(hWnd, format_options_verbose(
                g_graph, srcId, dstId,
                g_last_class,
                g_last_has_direct, g_last_direct_cost, g_last_direct_dist,
                g_last_by_segments
            ));
            return 0;
        }

        if (id == IDC_EXPORT && code == BN_CLICKED) {
            show_error(hWnd, L"Export JSON", L"Export JSON non inclus dans cette variante.");
            return 0;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"PeageTopNWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassW(&wc)) return 0;

    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"Optimisation Peage - Options par sorties (Classe 1..5)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 690, 540,
        nullptr, nullptr, hInst, nullptr
    );

    if (!hWnd) return 0;
    ShowWindow(hWnd, nCmdShow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
