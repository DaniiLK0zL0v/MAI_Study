#include <iostream>
#include <vector>

using namespace std;

vector<int> parent_node;

int find_set(int v) {
    if (v == parent_node[v]) {
        return v;
    }
    return parent_node[v] = find_set(parent_node[v]);
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m;
    if (cin >> n >> m) {
        parent_node.resize(n + 1);
        for (int i = 1; i <= n; ++i) {
            parent_node[i] = i;
        }
        
        int components = n;
        for (int i = 0; i < m; ++i) {
            int u, v;
            cin >> u >> v;
            int root_u = find_set(u);
            int root_v = find_set(v);
            if (root_u != root_v) {
                parent_node[root_u] = root_v;
                components--;
            }
        }
        
        cout << components << "\n";
    }
    return 0;
}
