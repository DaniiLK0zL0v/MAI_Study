#include <iostream>
#include <algorithm>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    long long x1, y1, x2, y2;
    long long x3, y3, x4, y4;
    
    if (cin >> x1 >> y1 >> x2 >> y2 >> x3 >> y3 >> x4 >> y4) {
        long long min_x1 = min(x1, x2), max_x1 = max(x1, x2);
        long long min_y1 = min(y1, y2), max_y1 = max(y1, y2);
        
        long long min_x2 = min(x3, x4), max_x2 = max(x3, x4);
        long long min_y2 = min(y3, y4), max_y2 = max(y3, y4);
        
        long long s1 = (max_x1 - min_x1) * (max_y1 - min_y1);
        long long s2 = (max_x2 - min_x2) * (max_y2 - min_y2);
        
        long long inter_min_x = max(min_x1, min_x2);
        long long inter_max_x = min(max_x1, max_x2);
        long long inter_min_y = max(min_y1, min_y2);
        long long inter_max_y = min(max_y1, max_y2);
        
        long long inter_s = 0;
        if (inter_max_x > inter_min_x && inter_max_y > inter_min_y) {
            inter_s = (inter_max_x - inter_min_x) * (inter_max_y - inter_min_y);
        }
        
        cout << s1 + s2 - inter_s << "\n";
    }
    return 0;
}