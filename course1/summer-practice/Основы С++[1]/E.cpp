#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> a(n);
        int global_min_pos = 0;
        int global_max_pos = 0;
        
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
            if (a[i] < a[global_min_pos]) {
                global_min_pos = i;
            }
            if (a[i] > a[global_max_pos]) {
                global_max_pos = i;
            }
        }
        
        vector<int> local_mins;
        vector<int> local_maxs;
        
        for (int i = 0; i < n; ++i) {
            bool is_min = true;
            bool is_max = true;
            
            if (i > 0) {
                if (a[i] > a[i - 1]) is_min = false;
                if (a[i] < a[i - 1]) is_max = false;
            }
            if (i < n - 1) {
                if (a[i] > a[i + 1]) is_min = false;
                if (a[i] < a[i + 1]) is_max = false;
            }
            if (is_min) {
                local_mins.push_back(i + 1);
            }
            if (is_max) {
                local_maxs.push_back(i + 1);
            }
        }
        
        cout << global_min_pos + 1 << " " << global_max_pos + 1 << "\n";
        for (int i = 0; i < (int)local_mins.size(); ++i) {
            cout << local_mins[i] << (i == (int)local_mins.size() - 1 ? "" : " ");
        }
        cout << "\n";
        for (int i = 0; i < (int)local_maxs.size(); ++i) {
            cout << local_maxs[i] << (i == (int)local_maxs.size() - 1 ? "" : " ");
        }
        cout << "\n";
    }
    return 0;
}