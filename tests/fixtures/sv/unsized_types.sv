// Signals without a static bit width - a queue, a dynamic array, a string -
// are no hardware. They are not modelled (no graph node, one warning each)
// and the analysis is incomplete; they never become 1-bit signals. The
// fixed-size crossing a_r -> q is analyzed as usual.
module unsized_types (
    input  logic       clk_a,
    input  logic       clk_b,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [7:0] a_r;
    logic [7:0] hist_q[$];
    logic [7:0] dyn[];
    string      tag;

    always_ff @(posedge clk_a) a_r <= d;
    always_ff @(posedge clk_b) q <= a_r;

    always_ff @(posedge clk_b) begin
        hist_q <= {hist_q, a_r};
        dyn    <= new[2];
        tag    <= "b";
    end
endmodule
