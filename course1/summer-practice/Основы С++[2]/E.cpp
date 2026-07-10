#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> d(n);
        for (int i = 0; i < n; ++i) {
            cin >> d[i];
        }
        
        sort(d.begin(), d.end());
        
        int q;
        if (cin >> q) {
            for (int i = 0; i < q; ++i) {
                int s, f;
                cin >> s >> f;
                
                int l = min(s, f);
                int r = max(s, f);
                
                auto it1 = upper_bound(d.begin(), d.end(), l);
                auto it2 = lower_bound(d.begin(), d.end(), r);
                
                int ans = it2 - it1;
                cout << ans << "\n";
            }
        }
    }
    return 0;
}
