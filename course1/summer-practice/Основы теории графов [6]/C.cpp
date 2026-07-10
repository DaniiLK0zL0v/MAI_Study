#include <iostream>
#include <vector>

using namespace std;

vector<vector<int>> adj;
vector<bool> visited;
vector<int> path;

bool dfs(int curr, int target) {
    visited[curr] = true;
    path.push_back(curr);
    
    if (curr == target) {
        return true;
    }
    
    for (int next_node : adj[curr]) {
        if (!visited[next_node]) {
            if (dfs(next_node, target)) {
                return true;
            }
        }
    }
    
    path.pop_back();
    return false;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m, u, v;
    if (cin >> n >> m >> u >> v) {
        adj.resize(n + 1);
        visited.assign(n + 1, false);
        
        for (int i = 0; i < m; ++i) {
            int x, y;
            cin >> x >> y;
            adj[x].push_back(y);
            adj[y].push_back(x);
        }
        
        if (dfs(u, v)) {
            for (int i = 0; i < (int)path.size(); ++i) {
                cout << path[i] << (i == (int)path.size() - 1 ? "" : " ");
            }
            cout << "\n";
        } else {
            cout << -1 << "\n";
        }
    }
    return 0;
}
