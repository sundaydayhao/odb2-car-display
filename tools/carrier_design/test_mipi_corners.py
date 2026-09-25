"""Native KiCad regressions for the via-end compensation defect.

Run with KiCad's Python: python -m unittest discover -s tools/carrier_design
-p test_mipi_corners.py. No project files are modified.
"""
import unittest
import pcbnew as k
from route_mipi import tuned_geometry,lengths
from review_routing import review_mipi_corners


def board_from_paths(paths):
    board=k.BOARD()
    for name,chunks in paths.items():
        net=k.NETINFO_ITEM(board,name);board.Add(net)
        for layer,points in chunks:
            for a,b in zip(points,points[1:]):
                t=k.PCB_TRACK(board)
                t.SetStart(k.VECTOR2I(k.FromMM(a[0]+50),k.FromMM(a[1]+50)))
                t.SetEnd(k.VECTOR2I(k.FromMM(b[0]+50),k.FromMM(b[1]+50)))
                t.SetLayer(layer);t.SetNetCode(net.GetNetCode());board.Add(t)
    return board


class MipiCorners(unittest.TestCase):
    def test_previous_via_triangles_are_rejected(self):
        # Actual vertices of the four corners present in revision 8c2a274.
        cases={
            'DSI_D0_P':[(73.5,35.8),(72.9642025106,35.3),(73.869,35.1)],
            'DSI_D1_P':[(69.5,35.8),(68.9642025106,35.3),(69.869,35.1)],
            'CSI_D0_P':[(77.5,35.8),(78.0357974894,35.3),(77.131,35.1)],
            'CSI_D1_P':[(83,31.5),(83.4713701461,31.0286298539),(82.1,31.869)],
        }
        for net,points in cases.items():
            with self.subTest(net=net):
                report=review_mipi_corners(board_from_paths({net:[(k.B_Cu,points)]}))
                self.assertEqual(report['acute_corners'],1)
                self.assertEqual(len(report['violations']),1)

    def test_corner_at_chunk_boundary_is_checked(self):
        paths={'DSI_D0_P':[(k.B_Cu,[(0,0),(1,0)]),(k.B_Cu,[(1,0),(1,1)])]}
        report=review_mipi_corners(board_from_paths(paths))
        self.assertEqual(len(report['violations']),1)
        self.assertAlmostEqual(report['minimum_interior_angle_degrees'],90)

    def test_generated_six_pairs_keep_skew_without_sharp_corners(self):
        paths,_,_,_=tuned_geometry()
        report=review_mipi_corners(board_from_paths(paths))
        self.assertEqual(report['violations'],[])
        self.assertGreaterEqual(report['minimum_interior_angle_degrees'],134.99)
        self.assertGreater(report['junctions_checked'],150)
        measured=lengths(paths)
        self.assertEqual(len(measured),12)
        for net in measured:
            if net.endswith('_P'):
                self.assertLess(abs(measured[net]-measured[net[:-1]+'N']),.001)


if __name__=='__main__':unittest.main()
