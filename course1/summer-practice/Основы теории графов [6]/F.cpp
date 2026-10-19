#include <iostream>
#include <vector>
#include <string>
#include <queue>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m;
    if (cin >> n >> m) {
        vector<string> grid(n);
        for (int i = 0; i < n; ++i) {
            cin >> grid[i];
        }
        
        int dr[] = {-1, 1, 0, 0};
        int dc[] = {0, 0, -1, 1};
        
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < m; ++c) {
                char ch = grid[r][c];

                if (ch != '#' && ch != '.') {

                    queue<pair<int, int>> q;
                    q.push({r, c});
                    
                    while (!q.empty()) {
                        auto [curr_r, curr_c] = q.front();
                        q.pop();
                        
                        for (int i = 0; i < 4; ++i) {
                            int nr = curr_r + dr[i];
                            int nc = curr_c + dc[i];
                            
                            if (nr >= 0 && nr < n && nc >= 0 && nc < m) {
                                if (grid[nr][nc] == '.') {
                                    grid[nr][nc] = ch;
                                    q.push({nr, nc});
                                }
                            }
                        }
                    }
                }
            }
        }
        
        for (int i = 0; i < n; ++i) {
            cout << grid[i] << "\n";
        }
    }
    return 0;
}
