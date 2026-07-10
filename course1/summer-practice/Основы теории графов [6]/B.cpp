#include <iostream>
#include <vector>
#include <string>

using namespace std;

int dx[] = {-1, 1, 0, 0};
int dy[] = {0, 0, -1, 1};

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m;
    if (cin >> n >> m) {
        vector<string> grid(n);
        for (int i = 0; i < n; ++i) {
            cin >> grid[i];
        }
        
        int sx, sy;
        cin >> sx >> sy;
        sx--; // convert to 0-based
        sy--;
        
        vector<vector<int>> dist(n, vector<int>(m, -1));
        dist[sx][sy] = 0;
        
        vector<pair<int, int>> q;
        q.push_back({sx, sy});
        int head = 0;
        
        while (head < (int)q.size()) {
            pair<int, int> cur = q[head++];
            int cx = cur.first;
            int cy = cur.second;
            
            for (int d = 0; d < 4; ++d) {
                int nx = cx + dx[d];
                int ny = cy + dy[d];
                
                if (nx >= 0 && nx < n && ny >= 0 && ny < m) {
                    if (grid[nx][ny] == '.' && dist[nx][ny] == -1) {
                        dist[nx][ny] = dist[cx][cy] + 1;
                        q.push_back({nx, ny});
                    }
                }
            }
        }
        
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) {
                cout << dist[i][j] << (j == m - 1 ? "" : " ");
            }
            cout << "\n";
        }
    }
    return 0;
}
