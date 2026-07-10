#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>

using namespace std;

const int INF = 2e9;

struct Edge {
    int to;
    int weight;
};

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m, s;
    if (cin >> n >> m >> s) {
        vector<vector<Edge>> adj(n + 1);
        for (int i = 0; i < m; ++i) {
            int u, v, w;
            cin >> u >> v >> w;
            adj[u].push_back({v, w});
            adj[v].push_back({u, w});
        }
        
        vector<int> width(n + 1, 0);
        width[s] = INF;
        
        // max-priority queue storing pairs: {width, vertex}
        priority_queue<pair<int, int>> pq;
        pq.push({INF, s});
        
        while (!pq.empty()) {
            auto [w, u] = pq.top();
            pq.pop();
            
            if (w < width[u]) {
                continue;
            }
            
            for (const auto& edge : adj[u]) {
                int next_width = min(w, edge.weight);
                if (next_width > width[edge.to]) {
                    width[edge.to] = next_width;
                    pq.push({next_width, edge.to});
                }
            }
        }
        
        for (int i = 1; i <= n; ++i) {
            if (i == s) {
                cout << -1 << (i == n ? "" : " ");
            } else {
                cout << width[i] << (i == n ? "" : " ");
            }
        }
        cout << "\n";
    }
    return 0;
}
