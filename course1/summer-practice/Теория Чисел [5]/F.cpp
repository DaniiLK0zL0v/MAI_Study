#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> cnt(8, 0);
        for (int i = 0; i < n; ++i) {
            int val;
            cin >> val;
            if (val >= 1 && val <= 7) {
                cnt[val]++;
            }
        }
        
        if (cnt[5] == 0 && cnt[7] == 0 &&
            cnt[2] >= cnt[4] &&
            (cnt[2] - cnt[4] + cnt[3] == cnt[6]) &&
            (cnt[2] + cnt[3] == cnt[1]) &&
            (cnt[1] == n / 3)) {
            
            int x = cnt[4];
            int y = cnt[2] - cnt[4];
            int z = cnt[3];
            
            for (int i = 0; i < x; ++i) {
                cout << "1 2 4\n";
            }
            for (int i = 0; i < y; ++i) {
                cout << "1 2 6\n";
            }
            for (int i = 0; i < z; ++i) {
                cout << "1 3 6\n";
            }
        } else {
            cout << -1 << "\n";
        }
    }
    return 0;
}
