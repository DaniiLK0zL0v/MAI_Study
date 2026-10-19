#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m;
    if (cin >> n >> m) {
        vector<int> a(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
        }
        
        vector<int> b(m);
        vector<int> target_freq(101, 0);
        for (int i = 0; i < m; ++i) {
            cin >> b[i];
            if (b[i] <= 100) {
                target_freq[b[i]]++;
            }
        }
        
        if (n < m) {
            cout << 0 << "\n";
            return 0;
        }
        
        vector<int> window_freq(101, 0);
        int matches = 0;
        for (int v = 1; v <= 100; ++v) {
            if (target_freq[v] == 0) {
                matches++;
            }
        }
        
        auto add_val = [&](int val) {
            if (val > 100) return;
            if (window_freq[val] == target_freq[val]) {
                matches--;
            }
            window_freq[val]++;
            if (window_freq[val] == target_freq[val]) {
                matches++;
            }
        };
        
        auto remove_val = [&](int val) {
            if (val > 100) return;
            if (window_freq[val] == target_freq[val]) {
                matches--;
            }
            window_freq[val]--;
            if (window_freq[val] == target_freq[val]) {
                matches++;
            }
        };
        
        for (int i = 0; i < m; ++i) {
            add_val(a[i]);
        }
        
        vector<int> ans;
        if (matches == 100) {
            ans.push_back(1);
        }
        
        for (int i = m; i < n; ++i) {
            remove_val(a[i - m]);
            add_val(a[i]);
            if (matches == 100) {
                ans.push_back(i - m + 2);
            }
        }
        
        cout << ans.size() << "\n";
        for (int i = 0; i < (int)ans.size(); ++i) {
            cout << ans[i] << (i == (int)ans.size() - 1 ? "" : " ");
        }
        if (!ans.empty()) {
            cout << "\n";
        }
    }
    return 0;
}
