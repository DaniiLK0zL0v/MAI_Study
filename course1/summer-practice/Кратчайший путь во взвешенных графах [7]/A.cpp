#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

struct Edge {
    int u, v;
    long long w;
};

const long long INF = 1e17;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m, s;
    if (cin >> n >> m >> s) {
        vector<Edge> edges(m);
        for (int i = 0; i < m; ++i) {
            cin >> edges[i].u >> edges[i].v >> edges[i].w;
        }
        
        vector<long long> dist(n + 1, INF);
        dist[s] = 0;
        
        for (int i = 0; i < n - 1; ++i) {
            bool relaxed = false;
            for (const auto& edge : edges) {
                if (dist[edge.u] < INF && dist[edge.u] + edge.w < dist[edge.v]) {
                    dist[edge.v] = dist[edge.u] + edge.w;
                    relaxed = true;
                }
            }
            if (!relaxed) {
                break;
            }
        }
        
        // n-th relaxation to check for negative cycles reachable from s
        bool has_negative_cycle = false;
        for (const auto& edge : edges) {
            if (dist[edge.u] < INF && dist[edge.u] + edge.w < dist[edge.v]) {
                has_negative_cycle = true;
                break;
            }
        }
        
        if (has_negative_cycle) {
            cout << "Negative cycle\n";
        } else {
            for (int i = 1; i <= n; ++i) {
                if (dist[i] >= INF) {
                    cout << "inf";
                } else {
                    cout << dist[i];
                }
                cout << (i == n ? "" : " ");
            }
            cout << "\n";
        }
    }
    return 0;
}
