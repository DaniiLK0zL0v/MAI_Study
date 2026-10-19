#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m, k;
    if (cin >> n >> m >> k) {
        vector<vector<int>> adj(n + 1);
        for (int i = 0; i < m; ++i) {
            int u, v;
            cin >> u >> v;
            adj[u].push_back(v);
            adj[v].push_back(u);
        }
        
        vector<int> dist(n + 1, -1);
        dist[k] = 0;
        
        vector<int> q;
        q.push_back(k);
        int head = 0;
        
        while (head < (int)q.size()) {
            int u = q[head++];
            for (int v : adj[u]) {
                if (dist[v] == -1) {
                    dist[v] = dist[u] + 1;
                    q.push_back(v);
                }
            }
        }
        
        for (int i = 1; i <= n; ++i) {
            cout << dist[i] << (i == n ? "" : " ");
        }
        cout << "\n";
    }
    return 0;
}
