#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> a(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
        }
        
        sort(a.begin(), a.end());
        
        long long count = 0;
        for (int i = 0; i < n; ++i) {
            int k = i + 2;
            for (int j = i + 1; j < n; ++j) {
                while (k < n && a[i] + a[j] > a[k]) {
                    k++;
                }
                if (k > j + 1) {
                    count += (k - 1 - j);
                }
            }
        }
        cout << count << "\n";
    }
    return 0;
}
