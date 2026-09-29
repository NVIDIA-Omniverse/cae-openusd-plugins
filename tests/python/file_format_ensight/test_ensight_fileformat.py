# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

import pathlib

import pytest
from pxr import OmniSci, OmniSciEnSight, Sdf, Usd

pytest.importorskip("pxr.OmniSci", reason="omniSci plugin not available")
pytest.importorskip("pxr.OmniSciEnSight", reason="omniSciEnSight plugin not available")


_DATA_DIR = pathlib.Path(__file__).parent.parent.parent / "data" / "EnSight"
_DISK_OUT = _DATA_DIR / "disk_out_ref.0.case"
_DISK_OUT_ENCAS = _DATA_DIR / "disk_out_ref.0.encas"
_NFACED = _DATA_DIR / "multicomb_o_nfaced.0.case"
_MIXED = _DATA_DIR / "disk_out_ref_mixed.0.case"


def _ensight_available() -> bool:
    return Sdf.FileFormat.FindById("OmniSciEnSightFileFormat") is not None


def _root_name(path: pathlib.Path) -> str:
    # The layer's default prim is named after the filename stem (extension
    # stripped); identifiers stay alphanumeric/underscore.
    return path.stem.replace(".", "_")


def _identifier(path: pathlib.Path, **kwargs) -> str:
    return Sdf.Layer.CreateIdentifier(str(path), kwargs)


def _first_part(stage: Usd.Stage, root_name: str):
    root = stage.GetPrimAtPath(f"/{root_name}")
    assert root
    children = root.GetChildren()
    assert children
    return children[0]


def _first_piece(part_prim):
    pieces = OmniSciEnSight.UnstructuredPartAPI(part_prim).GetPiecesRel().GetTargets()
    assert pieces
    return part_prim.GetStage().GetPrimAtPath(pieces[0])


@pytest.fixture(scope="module")
def disk_out_stage():
    if not _DISK_OUT.exists():
        pytest.skip("EnSight test data not found")
    if not _ensight_available():
        pytest.skip("OmniSciEnSightFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_DISK_OUT))
    assert stage
    return stage


@pytest.fixture(scope="module")
def nfaced_stage():
    if not _NFACED.exists():
        pytest.skip("EnSight nfaced test data not found")
    if not _ensight_available():
        pytest.skip("OmniSciEnSightFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_NFACED))
    assert stage
    return stage


@pytest.fixture(scope="module")
def mixed_stage():
    if not _MIXED.exists():
        pytest.skip("EnSight mixed test data not found")
    if not _ensight_available():
        pytest.skip("OmniSciEnSightFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_MIXED))
    assert stage
    return stage


@pytest.mark.integration
def test_plugin_registered():
    assert _ensight_available(), "OmniSciEnSightFileFormat not registered"


@pytest.mark.integration
def test_layer_authors_canonical_tcps(disk_out_stage):
    """EnSight layers self-describe simulation seconds with TCPS=1.0."""
    assert disk_out_stage.GetTimeCodesPerSecond() == pytest.approx(1.0)


@pytest.mark.integration
def test_default_prim_is_filename_stem(disk_out_stage):
    default_prim = disk_out_stage.GetDefaultPrim()
    assert default_prim
    assert default_prim.GetPath() == Sdf.Path(f"/{_root_name(_DISK_OUT)}")
    assert not disk_out_stage.GetPrimAtPath("/World").IsValid()


@pytest.mark.integration
def test_root_scope_exists(disk_out_stage):
    root = disk_out_stage.GetPrimAtPath(f"/{_root_name(_DISK_OUT)}")
    assert root


@pytest.mark.integration
def test_part_has_ensight_part_api(disk_out_stage):
    part = _first_part(disk_out_stage, _root_name(_DISK_OUT))
    assert OmniSciEnSight.UnstructuredPartAPI(part)


@pytest.mark.integration
def test_part_coordinates_load(disk_out_stage):
    part = _first_part(disk_out_stage, _root_name(_DISK_OUT))
    coords_x = part.GetAttribute("omni:sci:array:coordinatesX:value")
    coords_y = part.GetAttribute("omni:sci:array:coordinatesY:value")
    coords_z = part.GetAttribute("omni:sci:array:coordinatesZ:value")
    assert coords_x.IsValid()
    assert coords_y.IsValid()
    assert coords_z.IsValid()
    x = coords_x.Get(Usd.TimeCode.EarliestTime())
    y = coords_y.Get(Usd.TimeCode.EarliestTime())
    z = coords_z.Get(Usd.TimeCode.EarliestTime())
    assert x is not None and y is not None and z is not None
    assert len(x) > 0
    assert len(x) == len(y) == len(z)


@pytest.mark.integration
def test_heavy_arrays_are_time_sampled():
    if not _DISK_OUT.exists():
        pytest.skip("EnSight test data not found")
    if not _ensight_available():
        pytest.skip("OmniSciEnSightFileFormat plugin not registered")

    stage = Usd.Stage.Open(_identifier(_DISK_OUT))
    assert stage

    part = stage.GetPrimAtPath(f"/{_root_name(_DISK_OUT)}/VTK_Part")
    assert part
    coords_x = part.GetAttribute("omni:sci:array:coordinatesX:value")
    temp = part.GetAttribute("omni:sci:array:Temp_n:value")
    assert coords_x.GetTimeSamples() == [0.0]
    assert temp.GetTimeSamples() == [0.0]
    assert len(coords_x.Get(Usd.TimeCode(0))) > 0
    assert len(temp.Get(Usd.TimeCode(0))) > 0
    assert part.GetAttribute("omni:sci:array:coordinatesX:device").Get() == "cpu"

    piece = stage.GetPrimAtPath(f"/{_root_name(_DISK_OUT)}/VTK_Part/Piece_0")
    assert piece
    connectivity = piece.GetAttribute("omni:sci:array:connectivity:value")
    assert connectivity.GetTimeSamples() == [0.0]
    assert len(connectivity.Get(Usd.TimeCode(0))) > 0


@pytest.mark.integration
def test_node_scalar_field_loads(disk_out_stage):
    part = _first_part(disk_out_stage, _root_name(_DISK_OUT))
    attr = part.GetAttribute("omni:sci:array:Temp_n:value")
    assert attr.IsValid()
    values = attr.Get(Usd.TimeCode.EarliestTime())
    assert values is not None
    assert len(values) > 0


@pytest.mark.integration
def test_piece_connectivity_loads(disk_out_stage):
    part = _first_part(disk_out_stage, _root_name(_DISK_OUT))
    piece = _first_piece(part)
    assert piece.IsA(OmniSciEnSight.Piece)
    assert OmniSciEnSight.UnstructuredPieceAPI(piece)
    assert not piece.IsA(OmniSci.Dataset)
    assert piece.GetTypeName() == "OmniSciEnSightPiece"
    values = piece.GetAttribute("omni:sci:array:connectivity:value").Get(Usd.TimeCode.EarliestTime())
    assert values is not None
    assert len(values) > 0


@pytest.mark.integration
def test_nfaced_piece_has_count_arrays(nfaced_stage):
    part = _first_part(nfaced_stage, _root_name(_NFACED))
    piece = _first_piece(part)
    face_counts = piece.GetAttribute("omni:sci:array:elementFaceCounts:value").Get(Usd.TimeCode.EarliestTime())
    node_counts = piece.GetAttribute("omni:sci:array:faceNodeCounts:value").Get(Usd.TimeCode.EarliestTime())
    assert face_counts is not None
    assert node_counts is not None
    assert len(face_counts) > 0
    assert len(node_counts) > 0


@pytest.mark.integration
def test_mixed_case_exposes_node_and_element_fields(mixed_stage):
    part = _first_part(mixed_stage, _root_name(_MIXED))
    node_attr = part.GetAttribute("omni:sci:array:Temp_n_n_n:value")
    elem_attr = part.GetAttribute("omni:sci:array:Temp_n_n_c:value")
    assert node_attr.IsValid()
    assert elem_attr.IsValid()
    node_values = node_attr.Get(Usd.TimeCode.EarliestTime())
    elem_values = elem_attr.Get(Usd.TimeCode.EarliestTime())
    assert node_values is not None
    assert elem_values is not None
    assert len(node_values) > 0
    assert len(elem_values) > 0


@pytest.mark.integration
def test_mixed_case_loads_node_and_element_vectors(mixed_stage):
    part = _first_part(mixed_stage, _root_name(_MIXED))
    node_attr = part.GetAttribute("omni:sci:array:V_n_n_n:value")
    elem_attr = part.GetAttribute("omni:sci:array:V_n_n_c:value")
    assert node_attr.IsValid()
    assert elem_attr.IsValid()
    node_values = node_attr.Get(Usd.TimeCode.EarliestTime())
    elem_values = elem_attr.Get(Usd.TimeCode.EarliestTime())
    assert node_values is not None
    assert elem_values is not None
    assert len(node_values) > 0
    assert len(elem_values) > 0
    assert len(node_values[0]) == 3
    assert len(elem_values[0]) == 3


# ---------------------------------------------------------------------------
# .encas extension + whitespace-tolerant "type:" header tests
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def encas_stage():
    """Open the .encas variant of disk_out_ref — exercises the alias extension
    and the whitespace-tolerant 'type:  ensight gold' header (two spaces)."""
    if not _DISK_OUT_ENCAS.exists():
        pytest.skip("EnSight .encas test data not found")
    if not _ensight_available():
        pytest.skip("OmniSciEnSightFileFormat plugin not registered")
    stage = Usd.Stage.Open(str(_DISK_OUT_ENCAS))
    assert stage, f"Failed to open {_DISK_OUT_ENCAS}"
    return stage


@pytest.mark.integration
def test_encas_extension_opens(encas_stage):
    """The plugin must open .encas files (the 'encas' alias extension)."""
    assert encas_stage is not None


@pytest.mark.integration
def test_encas_can_read_returns_true():
    """CanRead must accept .encas files even when the type line has extra
    internal whitespace ('type:  ensight gold' with two spaces)."""
    if not _DISK_OUT_ENCAS.exists():
        pytest.skip("EnSight .encas test data not found")
    if not _ensight_available():
        pytest.skip("OmniSciEnSightFileFormat plugin not registered")
    fmt = Sdf.FileFormat.FindById("OmniSciEnSightFileFormat")
    assert fmt is not None
    assert fmt.CanRead(str(_DISK_OUT_ENCAS))


@pytest.mark.integration
def test_encas_default_prim_is_filename_stem(encas_stage):
    """Default prim should be derived from the .encas file's stem, not the
    primary 'case' extension."""
    default_prim = encas_stage.GetDefaultPrim()
    assert default_prim
    assert default_prim.GetPath() == Sdf.Path(f"/{_root_name(_DISK_OUT_ENCAS)}")


@pytest.mark.integration
def test_encas_coordinates_load(encas_stage):
    """Coordinates must be readable through the .encas entry point."""
    part = _first_part(encas_stage, _root_name(_DISK_OUT_ENCAS))
    x = part.GetAttribute("omni:sci:array:coordinatesX:value").Get(Usd.TimeCode.EarliestTime())
    assert x is not None and len(x) > 0


@pytest.mark.integration
def test_encas_field_loads(encas_stage):
    """Scalar field data must load when the file is opened via .encas."""
    part = _first_part(encas_stage, _root_name(_DISK_OUT_ENCAS))
    values = part.GetAttribute("omni:sci:array:Temp_n:value").Get(Usd.TimeCode.EarliestTime())
    assert values is not None and len(values) > 0
