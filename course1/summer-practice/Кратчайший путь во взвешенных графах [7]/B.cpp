#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>

using namespace std;

const long long INF = 1e18;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m;
    if (cin >> n >> m) {
        vector<vector<pair<int, long long>>> adj(n + 1);
        for (int i = 0; i < m; ++i) {
            int u, v;
            long long w;
            cin >> u >> v >> w;
            adj[u].push_back({v, w});
        }
        
        vector<long long> dist(n + 1, INF);
        vector<int> parent(n + 1, -1);
        
        priority_queue<pair<long long, int>, vector<pair<long long, int>>, greater<pair<long long, int>>> pq;
        
        dist[1] = 0;
        pq.push({0, 1});
        
        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();
            
            if (d > dist[u]) {
                continue;
            }
            
            for (const auto& edge : adj[u]) {
                int v = edge.first;
                long long w = edge.second;
                if (dist[u] + w < dist[v]) {
                    dist[v] = dist[u] + w;
                    parent[v] = u;
                    pq.push({dist[v], v});
                }
            }
        }
        
        if (dist[n] == INF) {
            cout << -1 << "\n";
        } else {
            vector<int> path;
            for (int curr = n; curr != -1; curr = parent[curr]) {
                path.push_back(curr);
            }
            reverse(path.begin(), path.end());
            
            for (size_t i = 0; i < path.size(); ++i) {
                cout << path[i] << (i == path.size() - 1 ? "" : " ");
            }
            cout << "\n";
        }
    }
    return 0;
}
