#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    long long omega;
    if (cin >> n >> omega) {
        vector<pair<long long, int>> holes(n);
        for (int i = 0; i < n; ++i) {
            cin >> holes[i].first;
            holes[i].second = i + 1;
        }
        
        sort(holes.begin(), holes.end());
        
        bool ok = true;
        long long current_omega = omega;
        for (int i = 0; i < n; ++i) {
            if (current_omega > holes[i].first) {
                current_omega += holes[i].first;
            } else {
                ok = false;
                break;
            }
        }
        
        if (ok) {
            cout << "YES\n";
            for (int i = 0; i < n; ++i) {
                cout << holes[i].second << (i == n - 1 ? "" : " ");
            }
            cout << "\n";
        } else {
            cout << "NO\n";
        }
    }
    return 0;
}
