#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m, u, v;
    if (cin >> n >> m >> u >> v) {
        vector<vector<int>> adj(n + 1);
        for (int i = 0; i < m; ++i) {
            int x, y;
            cin >> x >> y;
            adj[x].push_back(y);
            adj[y].push_back(x);
        }
        
        vector<int> dist(n + 1, -1);
        dist[v] = 0;
        
        vector<int> q;
        q.push_back(v);
        int head = 0;
        
        while (head < (int)q.size()) {
            int curr = q[head++];
            for (int next_node : adj[curr]) {
                if (dist[next_node] == -1) {
                    dist[next_node] = dist[curr] + 1;
                    q.push_back(next_node);
                }
            }
        }
        
        if (dist[u] == -1) {
            cout << -1 << "\n";
        } else {
            vector<int> path;
            int curr = u;
            path.push_back(curr);
            
            while (curr != v) {
                int best_next = -1;
                for (int next_node : adj[curr]) {
                    if (dist[next_node] == dist[curr] - 1) {
                        if (best_next == -1 || next_node < best_next) {
                            best_next = next_node;
                        }
                    }
                }
                curr = best_next;
                path.push_back(curr);
            }
            
            for (int i = 0; i < (int)path.size(); ++i) {
                cout << path[i] << (i == (int)path.size() - 1 ? "" : " ");
            }
            cout << "\n";
        }
    }
    return 0;
}
